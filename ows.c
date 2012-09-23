#include "ows.h"

#define TIMESLOT_WAIT_TIMEOUT 120
// timer prescaler is 1/64
#define uS_TO_TIMER_COUNTS(t) (((t) * CLK_FREQ) / 64 / 1000L)

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include <avr/eeprom.h>
#ifdef OWS_SPM_ENABLE
#include "ows_spm.h"
#endif

// ows private data
char ows_rom[8];
uint8_t errno;
#ifdef OWS_WRITE_ROM_ENABLE
uint16_t ows_eeprom_addr;
#endif
#ifdef OWS_CONDSEARCH_ENABLE
uint8_t ows_flag;
# define OWS_FLAG_INTERRUPT_POSSIBLE 0x80
#endif

inline void ows_pull_bus_down()
{
    OWPORT(DDR) |= (OWMASK);    // drives output low
}

inline void ows_release_bus()
{
    OWPORT(DDR) &= ~(OWMASK);
}

inline uint8_t ows_read_bus()
{
    return (OWPORT(PIN) & OWMASK) ? 1 : 0;
}

inline void ows_delay_15uS() // delayMicroseconds(15)
{
    uint16_t us = (15L * CLK_FREQ) / 4L / 1000L;
    // account for the time taken in the preceeding commands.
    us -= 2;

    // busy wait
    __asm__ __volatile__ (
        "1: sbiw %0,1" "\n\t" // 2 cycles
        "brne 1b" : "=w" (us) : "0" (us) // 2 cycles
    );
}
inline void ows_delay_30uS() // delayMicroseconds(30)
{
    ows_delay_15uS();
    ows_delay_15uS();
}

volatile int16_t ows_timestamp;
inline void ows_timer_start(int16_t timeout)
{
  ows_timestamp = timeout;
  TCCR0A = 0x00; // Normal mode
  TCCR0B = 0x03; // clk/64
#ifdef TIMSK0
  TIMSK0 = 0x00; // disable timer interrupts
#else
  TIMSK = 0x00; // disable timer interrupts
#endif
  TCNT0 = 0; // count register
}

inline void ows_timer_stop()
{
  TCCR0B = 0x00; // clk/64
}

// 70 .. 540 uS --- Reset pulse
// 7520 .. 8640 clks @ 16MHz (1 clk every 0.0625 uS) (Mega)
// 4512 .. 5184 clks @ 9.6MHz (1 clk every 0.104 uS) (Tyny45)
// prescalers: 8 64 256 1024

// prescaler: 64
// 117.5 .. 135 counts @ 16MHz
// 70.5 .. 81 counts @ 9.6MHz     ((540)/(1/9.6))/64

inline int16_t ows_timer_read()
{
  return ows_timestamp - TCNT0;
}

/* === end platform-specific === */

uint8_t ows_crc8(char* data, uint8_t len)
{
    uint8_t crc = 0;

    while (len--) {
        uint8_t inbyte = *data++;
        for (uint8_t i = 8; i; i--) {
            uint8_t mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            inbyte >>= 1;
        }
    }
    return crc;
}

void ows_setup(char * rom)
{
    for (int i=0; i<7; i++)
        ows_rom[i] = rom[i];
    ows_rom[7] = ows_crc8(ows_rom, 7);
    OWPORT(PORT) &= ~(OWMASK); /* We only need "0" - simulate open drain */
#ifdef GIFR /* tiny45 */
    GIMSK |= (1 << PCIE); /* enable pin change interrupts */
#else
    PCICR |= 0x07; /* enable all pin change interrupts */
#endif
    MCUCR = 1<<SE; /* sleep enable (idle mode) */
#ifdef OWS_CONDSEARCH_ENABLE
    ows_flag = 0;
#endif
}

void ows_setup2(uint8_t family, uint16_t eeprom_addr)
{
    ows_rom[0] = family;
#ifdef OWS_WRITE_ROM_ENABLE
    ows_eeprom_addr = eeprom_addr;
#endif
    eeprom_read_block((void*)&ows_rom[1], (const void*)eeprom_addr, 6);
    ows_rom[7] = ows_crc8(ows_rom, 7);
    OWPORT(PORT) &= ~(OWMASK); /* We only need "0" - simulate open drain */
#ifdef GIFR /* tiny45 */
    GIMSK |= (1 << PCIE); /* enable pin change interrupts */
#else
    PCICR |= 0x07; /* enable all pin change interrupts */
#endif
    MCUCR = 1<<SE; /* sleep enable (idle mode) */
#ifdef OWS_CONDSEARCH_ENABLE
    ows_flag = 0;
#endif
}

uint8_t ows_presence();
uint8_t ows_in_reset();

uint8_t ows_wait_reset() {
    errno = ONEWIRE_NO_ERROR;
    ows_release_bus(); /* just in case */
    OWPCMSK |= OWMASK; /* enable pin change interrupt here, global interrupts are still disabled */
    if(ows_read_bus()) {
        sei();
        sleep_cpu();
        cli();
    }
    OWPCMSK &= ~OWMASK; /* disable pin change interrupt here, global interrupts are still disabled */
    if(ows_read_bus()) {
        errno = ONEWIRE_INTERRUPTED;
        return 0;
    }
    return ows_in_reset();
}

uint8_t ows_in_reset()
{
    /* if just woken up: it gets ~117uS to wake up tiny45! */
    /* if from recv_bit: ~120uS alrealy passed */
    /* new experiment: ~170uS passed */
    ows_timer_start(uS_TO_TIMER_COUNTS(540 - 170 /* (120UL*8000UL/CLK_FREQ) */));
#ifdef OWS_INTERRUPTS_ENABLE
    if(ows_flag & OWS_FLAG_INT_TYPE2) {
        /* extend reset up to 960..4800uS */
        while (ows_read_bus() == 0) {
            if (ows_timer_read() > uS_TO_TIMER_COUNTS(540 - 400)) {
                ows_pull_bus_down();
                ows_timer_stop();
                for(uint8_t i = 20; i; --i) /* 400 + 20*30 = 1mS approx. */
                    ows_delay_30uS();
                ows_release_bus();
                ows_delay_30uS();
                return 1;
            }
        }
    } else
#endif /* OWS_INTERRUPTS_ENABLE */
    {
        while (ows_read_bus() == 0) {
            if (ows_timer_read() <= 0) {
                ows_timer_stop();
            }
        }
    }
    if (ows_timer_read() > uS_TO_TIMER_COUNTS(70)) {
        errno = ONEWIRE_VERY_SHORT_RESET;
        return 0;
    }
    ows_delay_30uS();
    return ows_presence();
}

uint8_t ows_presence()
{
    errno = ONEWIRE_NO_ERROR;
    ows_pull_bus_down();
    // 120uS delay
    ows_delay_30uS();
    ows_delay_30uS();
    ows_delay_30uS();
    ows_delay_30uS();
    ows_release_bus();
    //ows_delay(uS(300 - 25)); // XXX 25 - ?
    for(uint8_t t = 0; t < ((300 - 25)/30); ++t)
        ows_delay_30uS();
    if (! ows_read_bus()) {
        errno = ONEWIRE_PRESENCE_LOW_ON_LINE;
        return 0;
    } else
        return 1;
}

uint8_t ows_wait_time_slot()
{
    //arrive here just afer data sampling (1) or after releasing bus (0)
// CLK_FREQ in kHz, timeout in uS, 7 clocks per one 'while' cicle
#define TIMESLOT_WAIT_RETRY_COUNT \
  ((TIMESLOT_WAIT_TIMEOUT * CLK_FREQ) / 7L / 1000L)
    uint16_t retries;

    retries = TIMESLOT_WAIT_RETRY_COUNT; //shoud be 49uS, not 120
    while (! ows_read_bus())
        if (--retries == 0) {
            errno = ONEWIRE_TOO_LONG_PULSE;
            return 0;
        }
#if OWS_ENABLE_TIMESLOT_TIMEOUT
    retries = TIMESLOT_WAIT_RETRY_COUNT;
    while ( ows_read_bus())
        if (--retries == 0) {
            errno = ONEWIRE_TIMESLOT_TIMEOUT;
            return 0;
    }
#else
    while (ows_read_bus())
        ;
#endif /* OWS_ENABLE_TIMESLOT_TIMEOUT */
    return 1;
#undef TIMESLOT_WAIT_RETRY_COUNT
}

uint8_t ows_recv_bit(void)
{
    uint8_t r;

    ows_release_bus();
    if (!ows_wait_time_slot() )
        return 0;
    ows_delay_15uS();
    r = ows_read_bus();
    return r;
}

uint8_t ows_recv()
{
    uint8_t r = 0;

    errno = ONEWIRE_NO_ERROR;
    for (uint8_t bitmask = 0x01; bitmask && (errno == ONEWIRE_NO_ERROR); bitmask <<= 1)
        if (ows_recv_bit())
            r |= bitmask;
    return r;
}

void ows_send_bit(uint8_t v)
{
    ows_release_bus();
    if (!ows_wait_time_slot() )
        return;
    if (v & 1)
        ows_delay_30uS();
    else {
        ows_pull_bus_down();
        ows_delay_30uS();
        ows_release_bus();
    }
    return;
}

void ows_send(uint8_t v)
{
    errno = ONEWIRE_NO_ERROR;
    for (uint8_t bitmask = 0x01; bitmask && (errno == ONEWIRE_NO_ERROR); bitmask <<= 1)
        ows_send_bit((bitmask & v)?1:0);
}

uint8_t ows_send_data(const char buf[], uint8_t len)
{
    for (uint8_t i = 0; i < len; ++i) {
        ows_send(buf[i]);
        if (errno != ONEWIRE_NO_ERROR)
            return i;
    }
    return len;
}

uint8_t ows_search() {
    uint8_t bitmask;
    uint8_t bit_send, bit_recv;

    for (int i=0; i<8; i++) {
        for (bitmask = 0x01; bitmask; bitmask <<= 1) {
            bit_send = (bitmask & ows_rom[i])?1:0;
            ows_send_bit(bit_send);
            ows_send_bit(!bit_send);
            bit_recv = ows_recv_bit();
            if (errno != ONEWIRE_NO_ERROR)
                return 0;
            if (bit_recv != bit_send)
                return 0;
        }
    }
    return 1;
}

uint8_t ows_recv_data(char buf[], uint8_t len) {
    uint8_t bytes_received = 0;

    for (int i=0; i<len; i++) {
        buf[i] = ows_recv();
        if (errno != ONEWIRE_NO_ERROR)
            break;
        bytes_received++;
    }
    return bytes_received;
}

uint8_t ows_recv_process_cmd() {
    char addr[8];

    for (;;) {
      switch (ows_recv() ) {
        case 0xF0: // SEARCH ROM
            ows_search();
            return 0;
        case 0x33: // READ ROM
            ows_send_data(ows_rom, 8);
            if (errno != ONEWIRE_NO_ERROR)
                return 0;
            break;
#ifdef OWS_WRITE_ROM_ENABLE
        case 0xD5: // WRITE ROM
            ows_recv_data(addr, 8);
            if (errno != ONEWIRE_NO_ERROR)
                return 0;
            if (addr[0] != ows_rom[0] || addr[7] != ows_crc8(addr, 7))
                return 0;
            eeprom_busy_wait();
            eeprom_write_block(&addr[1], (void*)ows_eeprom_addr, 6);
            ows_send_data(ows_rom, 8);
            return 0;
#endif /* OWS_WRITE_ROM_ENABLE */
#ifdef OWS_CONDSEARCH_ENABLE
        case 0xEC: // CONDITIONAL SEARCH
            if(ows_flag & OWS_FLAG_CONDSEARCH)
                ows_search();
            return 0;
#endif
#ifdef OWS_SPM_ENABLE
        case 0xDA:
            ows_spm();
            break;
#endif
        case 0x55: // MATCH ROM
            ows_recv_data(addr, 8);
            if (errno != ONEWIRE_NO_ERROR)
                return 0;
            for (int i=0; i<8; i++)
                if (ows_rom[i] != addr[i])
                    return 0;
#ifdef OWS_CONDSEARCH_ENABLE
            ows_flag = 0;
#endif
            return 1;
        case 0xCC: // SKIP ROM
            return 1;
        default: // Unknow command
            if (errno == ONEWIRE_NO_ERROR)
                break; // skip if no error
            else
                return 0;
      }
    }
}

uint8_t ows_wait_request()
{
    errno = ONEWIRE_NO_ERROR;
    while(errno != ONEWIRE_INTERRUPTED) {
        if (! ows_wait_reset()) {
            if(errno == ONEWIRE_INTERRUPTED)
                return 0;
            else
                continue;
        }
        if (ows_recv_process_cmd() )
            return 1;
        else if (errno == ONEWIRE_NO_ERROR)
            continue;
        else if (errno == ONEWIRE_TOO_LONG_PULSE)
            ows_in_reset();
        else
            return 0;
    }
    return 0; /* Interruped */
}

#ifdef OWS_CONDSEARCH_ENABLE
static void ows_generate_spontaneous_interrupt()
{
    ows_pull_bus_down();
    for(uint8_t i = 33; i; --i) /* 33*30 = 990uS approx. */
        ows_delay_30uS();
    ows_release_bus();
    ows_delay_30uS();
    ows_presence();
}

void ows_set_flag(enum ows_flag_type f)
{
    ows_flag = (ows_flag & ~OWS_FLAG_MASK) | (f & OWS_FLAG_MASK);
# ifdef OWS_INTERRUPTS_ENABLE
    if(f & OWS_FLAG_INT_TYPE1 && (ows_flag && OWS_FLAG_INTERRUPT_POSSIBLE))
        ows_generate_spontaneous_interrupt();
# endif
}
#endif /* OWS_CONDSEARCH_ENABLE */

/*
 vim: ts=4 sw=4 sts=4 et
*/


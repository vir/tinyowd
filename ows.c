#include "ows.h"

#define TIMESLOT_WAIT_TIMEOUT 120
// timer prescaler is 1/64
#define uS_TO_TIMER_COUNTS(t) (((t) * CLK_FREQ) / 64 / 1000L)

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include <avr/eeprom.h>
#include <setjmp.h>

static jmp_buf err;

struct {
    int rc:1; /* resume flag */
    int wait_reset:1;
} ows_flags;

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

static void ows_delay_15uS() // delayMicroseconds(15)
{
    uint16_t us = (15L * CLK_FREQ) / 4L / 1000L;
    // account for the time taken in the preceeding commands.
    us -= 2 + 4 /* 4 for rcall */;

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
  TIMSK0 &= ~(1<<OCIE0A | 1<<OCIE0B | 1<<TOIE0); // disable timer 0 interrupts
#else
  TIMSK &= ~(1<<OCIE0A | 1<<OCIE0B | 1<<TOIE0); // disable timer 0 interrupts
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
    *(uint8_t*)&ows_flags = 0;
    ows_flags.wait_reset = 1;
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
    *(uint8_t*)&ows_flags = 0;
    ows_flags.wait_reset = 1;
}

void ows_presence();
void ows_in_reset();

void ows_wait_reset() {
    if(errno != ONEWIRE_TOO_LONG_PULSE)
    {
        errno = ONEWIRE_NO_ERROR;
        ows_release_bus(); /* just in case */
        OWPCMSK |= OWMASK; /* enable pin change interrupt here, global interrupts are still disabled */
        if(ows_read_bus()) {
            sei();
            sleep_cpu();
            cli();
        }
        OWPCMSK &= ~OWMASK; /* disable pin change interrupt here, global interrupts are still disabled */
        if(ows_read_bus())
            longjmp(err, ONEWIRE_INTERRUPTED);
    }
    ows_in_reset();
}

void ows_in_reset()
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
                return;
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
    if (ows_timer_read() > uS_TO_TIMER_COUNTS(70))
        longjmp(err, ONEWIRE_VERY_SHORT_RESET);
    ows_delay_30uS();
    ows_presence();
}

void ows_presence()
{
    ows_pull_bus_down();
    // 120uS delay
    ows_delay_30uS();
    ows_delay_30uS();
    ows_delay_30uS();
    ows_delay_30uS();
    ows_release_bus();
#if 0 /* no reason to check this */
    //ows_delay(uS(300 - 25)); // XXX 25 - ?
    for(uint8_t t = 0; t < ((300 - 25)/30); ++t)
        ows_delay_30uS();
    if (! ows_read_bus())
        longjmp(err, ONEWIRE_PRESENCE_LOW_ON_LINE);
#endif
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
        if (--retries == 0)
            longjmp(err, ONEWIRE_TOO_LONG_PULSE);
#if OWS_ENABLE_TIMESLOT_TIMEOUT
    retries = TIMESLOT_WAIT_RETRY_COUNT;
    while ( ows_read_bus())
        if (--retries == 0)
            longjmp(err, ONEWIRE_TIMESLOT_TIMEOUT);
    }
#else
    while (ows_read_bus())
        ;
#endif /* OWS_ENABLE_TIMESLOT_TIMEOUT */
#ifdef OWS_CONDSEARCH_ENABLE
    ows_flag &= ~OWS_FLAG_INTERRUPT_POSSIBLE;
#endif
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
    for (uint8_t bitmask = 0x01; bitmask; bitmask <<= 1)
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
    for (uint8_t bitmask = 0x01; bitmask; bitmask <<= 1)
        ows_send_bit((bitmask & v)?1:0);
}

void ows_send_data(const char buf[], uint8_t len)
{
    for (uint8_t i = 0; i < len; ++i)
        ows_send(buf[i]);
}

uint8_t ows_search() {
    uint8_t bitmask;
    uint8_t bit_send, bit_recv;
    ows_flags.rc = 0;
    for (int i=0; i<8; i++) {
        for (bitmask = 0x01; bitmask; bitmask <<= 1) {
            bit_send = (bitmask & ows_rom[i])?1:0;
            ows_send_bit(bit_send);
            ows_send_bit(!bit_send);
            bit_recv = ows_recv_bit();
            if (bit_recv != bit_send)
                return 0;
        }
    }
    ows_flags.rc = 1;
    return 1;
}

void ows_recv_data(char buf[], uint8_t len) {
    for (int i=0; i<len; i++)
        buf[i] = ows_recv();
}

uint8_t ows_recv_process_cmd() {
    char addr[8];
    for (;;) {
      switch (ows_recv() ) {
        case 0xF0: // SEARCH ROM
            return ows_search();
        case 0x33: // READ ROM
        case 0x0F:
            ows_send_data(ows_rom, 8);
            break;
#ifdef OWS_WRITE_ROM_ENABLE
        case 0xD5: // WRITE ROM
            ows_recv_data(addr, 8);
            if(addr[0] == ows_rom[0] && addr[7] == ows_crc8(addr, 7))
            {
                eeprom_busy_wait();
                eeprom_write_block(&addr[1], (void*)ows_eeprom_addr, 6);
                for(uint8_t i = 0; i < 8; ++i)
                    ows_rom[i] = addr[i];
            }
            ows_send_data(ows_rom, 8);
            return 0;
#endif /* OWS_WRITE_ROM_ENABLE */
#ifdef OWS_CONDSEARCH_ENABLE
        case 0xEC: // CONDITIONAL SEARCH
            if(ows_flag & OWS_FLAG_CONDSEARCH)
                return ows_search();
#endif
        case 0x55: // MATCH ROM
            ows_recv_data(addr, 8);
            for (int i=0; i<8; i++)
                if (ows_rom[i] != addr[i])
                    return 0;
#ifdef OWS_CONDSEARCH_ENABLE
            ows_flag = 0;
#endif
            return 1;
        case 0xCC: // SKIP ROM
            return 1;
        case 0xA5: // RESUME
            return ows_flags.rc;
        default: // Unknow command
            return 0;
      }
    }
}

void __attribute__((weak)) ows_process_cmds() { }
void __attribute__((weak)) ows_process_interrupt() { }

uint8_t ows_wait_request()
{
    switch((errno = setjmp(err)))
    {
        case 0:
            if(ows_flags.wait_reset)
                ows_wait_reset();
            ows_flags.wait_reset = 0;
            ows_flags.rc = ows_recv_process_cmd();
            if(ows_flags.rc)
                ows_process_cmds();
            else
                ows_flags.wait_reset = 1;
            break;
        case ONEWIRE_TIMESLOT_TIMEOUT:
        case ONEWIRE_VERY_SHORT_RESET:
        case ONEWIRE_PRESENCE_LOW_ON_LINE:
            ows_flags.wait_reset = 1;
            break;
        case ONEWIRE_INTERRUPTED:
            ows_process_interrupt();
            break;
        case ONEWIRE_TOO_LONG_PULSE:
            ows_flags.wait_reset = 1;
            break;

    }
    return 0;
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


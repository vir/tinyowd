#include "ows.h"

#define TIMESLOT_WAIT_TIMEOUT 120
// timer prescaler is 1/64
#define uS_TO_TIMER_COUNTS(t) (((t) * CLK_FREQ) / 64 / 1000L)

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

// ows private data
char ows_rom[8];
uint8_t errno;

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

inline void ows_delay_30uS() // delayMicroseconds(30)
{
    uint8_t oldSREG;
    uint16_t us = (30L * CLK_FREQ) / 4L / 1000L;
    // account for the time taken in the preceeding commands.
    us -= 2;

    oldSREG = SREG;
    cli();

    // busy wait
    __asm__ __volatile__ (
        "1: sbiw %0,1" "\n\t" // 2 cycles
        "brne 1b" : "=w" (us) : "0" (us) // 2 cycles
    );

    SREG = oldSREG;
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
}

uint8_t ows_wait_reset() {
    errno = ONEWIRE_NO_ERROR;
    ows_release_bus();
    while(ows_read_bus()) { };

    ows_timer_start(uS_TO_TIMER_COUNTS(540));
    while (ows_read_bus() == 0) {
        if (ows_timer_read() < 0) {
            errno = ONEWIRE_VERY_LONG_RESET;
            return 0;
        }
    }
    if (ows_timer_read() > uS_TO_TIMER_COUNTS(70)) {
        errno = ONEWIRE_VERY_SHORT_RESET;
        return 0;
    }
    ows_delay_30uS();
    return 1;
}

uint8_t ows_presence() {
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
// CLK_FREQ in kHz, timeout in uS, 7 clocks per one 'while' cicle
#define TIMESLOT_WAIT_RETRY_COUNT \
  ((TIMESLOT_WAIT_TIMEOUT * CLK_FREQ) / 7L / 1000L)
    uint16_t retries;

    retries = TIMESLOT_WAIT_RETRY_COUNT;
    while (! ows_read_bus())
        if (--retries == 0)
            return 0;
    retries = TIMESLOT_WAIT_RETRY_COUNT;
    while ( ows_read_bus())
        if (--retries == 0)
            return 0;
    return 1;
#undef TIMESLOT_WAIT_RETRY_COUNT
}

uint8_t ows_recv_bit(void)
{
    uint8_t r;

    ows_release_bus();
    if (!ows_wait_time_slot() ) {
        errno = ONEWIRE_READ_TIMESLOT_TIMEOUT;
        return 0;
    }
    ows_delay_30uS();
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
    if (!ows_wait_time_slot() ) {
        errno = ONEWIRE_WRITE_TIMESLOT_TIMEOUT;
        return;
    }
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

uint8_t ows_send_data(char buf[], uint8_t len)
{
    uint8_t bytes_sended = 0;

    for (int i=0; i<len; i++) {
        ows_send(buf[i]);
        if (errno != ONEWIRE_NO_ERROR)
            break;
        bytes_sended++;
    }
    return bytes_sended;
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
        case 0x55: // MATCH ROM
            ows_recv_data(addr, 8);
            if (errno != ONEWIRE_NO_ERROR)
                return 0;
            for (int i=0; i<8; i++)
                if (ows_rom[i] != addr[i])
                    return 0;
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

uint8_t ows_wait_request(uint8_t ignore_errors)
{
    errno = ONEWIRE_NO_ERROR;
    for (;;) {
        if (!ows_wait_reset() )
            continue;
        if (!ows_presence() )
            continue;
        if (ows_recv_process_cmd() )
            return 1;
        else if ((errno == ONEWIRE_NO_ERROR) || ignore_errors)
            continue;
        else
            return 0;
    }
}

/*
 vim: ts=4 sw=4 sts=4 et
*/


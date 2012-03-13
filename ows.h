#ifndef OWS_H_INCLUDED
#define OWS_H_INCLUDED

#include <stdint.h>

/* check predefines with << avr-cpp -dM -mmcu=atmega168 ows.c | grep -i avr >> */
#if defined(__AVR_ATmega168__)
# warning ===== Configured for ATMega168 =====
/* Arduino Nano:
 Arduino pin:   D0 .. D7    D8 .. D13   A0 .. A5    A6    A7
 ATMega port:  PD0 .. PD7  PB0 .. PB5  PC0 .. PC5  ADC6  ADC7
*/
# define CLK_FREQ 16000L
# define OWMASK 0x80
# define OWPORT(x) x##D
# define PIO_PORT(p) (p##B) /* hardcoded pins 0(A) and 2(B) ==> pins 8 and 10 on Arduino nano */
#elif defined(__AVR_ATtiny85__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny25__) || defined(__AVR_ATtiny13__)
# warning ===== Configured for ATTiny(13,24,45,85) =====
# define CLK_FREQ 9600L
# define OWMASK 0x02
# define OWPORT(x) x##B
# define PIO_PORT(p) (p##B) /* hardcoded pins 0(A) and 2(B) */
#else
# error Unsupported MCU
#endif

enum ows_error_code {
    ONEWIRE_NO_ERROR = 0,
    ONEWIRE_READ_TIMESLOT_TIMEOUT  = 1,
    ONEWIRE_WRITE_TIMESLOT_TIMEOUT = 2,
    /*ONEWIRE_WAIT_RESET_TIMEOUT     = 3,*/
    ONEWIRE_VERY_LONG_RESET        = 4,
    ONEWIRE_VERY_SHORT_RESET       = 5,
    ONEWIRE_PRESENCE_LOW_ON_LINE   = 6,
};

uint8_t ows_wait_request(uint8_t ignore_errors);
void ows_setup(char * rom);
uint8_t ows_recv();
void ows_send(uint8_t v);
extern uint8_t errno;

#endif /* OWS_H_INCLUDED */

/*
 vim: ts=4 sw=4 sts=4 et
*/

#ifndef OWS_H_INCLUDED
#define OWS_H_INCLUDED

#include <stdint.h>

#define OWS_WRITE_ROM_ENABLE 1

/* check predefines with << avr-cpp -dM -mmcu=atmega168 ows.c | grep -i avr >> */
#if defined(__AVR_ATmega168__)
# pragma message ===== Configured for ATMega168 =====
/* Arduino Nano:
 Arduino pin:   D0 .. D7    D8 .. D13   A0 .. A5    A6    A7
 ATMega port:  PD0 .. PD7  PB0 .. PB5  PC0 .. PC5  ADC6  ADC7
*/
# define CLK_FREQ 16000L
# define OWMASK 0x80
# define OWPORT(x) x##D
# define OWPCMSK PCMSK2 /* PCMSK0 - Port B, PCMSK1 - Port C, PCMSK2 - Port D */
# define PIO_PORT(p) (p##B) /* hardcoded pins 0(A) and 2(B) ==> pins 8 and 10 on Arduino nano */
#elif defined(__AVR_ATtiny13__)
# pragma message ===== Configured for ATTiny(13) =====
# define CLK_FREQ 9600L
# define OWMASK 0x02
# define OWPORT(x) x##B
# define OWPCMSK PCMSK
# define PIO_PORT(p) (p##B) /* hardcoded pins 0(A) and 2(B) */
#elif defined(__AVR_ATtiny85__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny25__)
# pragma message ===== Configured for ATTiny(25,45,85) =====
# define CLK_FREQ 8000L
# define OWMASK 0x10
# define OWPORT(x) x##B
# define OWPCMSK PCMSK
# define PIO_PORT(p) (p##B) /* hardcoded pins b0(A), b1(B), b2(C), b3(D) */
#else
# error Unsupported MCU
#endif

#ifdef OWS_CONDSEARCH_ENABLE
enum ows_flag_type {
    OWS_FLAG_CONDSEARCH = 0x01,
# ifdef OWS_INTERRUPTS_ENABLE
    OWS_FLAG_INT_TYPE1  = 0x02,
    OWS_FLAG_INT_TYPE2  = 0x04,
    OWS_FLAG_MASK       = 0x0F,
# endif
};
void ows_set_flag(enum ows_flag_type f);
#endif

enum ows_error_code {
    ONEWIRE_NO_ERROR = 0,
    ONEWIRE_TIMESLOT_TIMEOUT       = 1,
    /*ONEWIRE_WAIT_RESET_TIMEOUT     = 3,*/
    ONEWIRE_VERY_SHORT_RESET       = 5,
    ONEWIRE_PRESENCE_LOW_ON_LINE   = 6,
    ONEWIRE_INTERRUPTED            = 7,
    ONEWIRE_TOO_LONG_PULSE         = 8,
};

void ows_wait_request();
void ows_setup(char * rom);
void ows_setup2(uint8_t family, uint16_t eeprom_addr);
uint8_t ows_crc8(char* data, uint8_t len);
uint8_t ows_recv();
void ows_recv_data(char buf[], uint8_t len);
void ows_send(uint8_t v);
void ows_send_data(const char buf[], uint8_t len);

/* override to add functionality */
void ows_process_cmds();
void ows_process_interrupt();

extern uint8_t errno;

#endif /* OWS_H_INCLUDED */

/*
 vim: ts=4 sw=4 sts=4 et
*/

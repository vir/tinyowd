#include <avr/io.h>
#include "ows.h"

void serial_init()
{
	/* PRUSART0 must be disabled by writing a logical zero */
	PRR &= ~(1 << PRUSART0);

#define FOSC CLK_FREQ * 1000 // Clock Speed
#define BAUD 9600
#define MYUBRR (FOSC/16/BAUD-1)
	/*Set baud rate */
	UBRR0H = (unsigned char)(MYUBRR>>8);
	UBRR0L = (unsigned char)MYUBRR;
	/* Enable receiver and transmitter */
	UCSR0B = (1<<RXEN0)|(1<<TXEN0);
	/* Set frame format: 8data, 1stop bit */
	UCSR0C = (3<<UCSZ00);
}

char serial_read_wait()
{
	/* Wait for data to be received */
	while ( !(UCSR0A & (1<<RXC0)) );
	if(UCSR0A & (1 << FE0))
		__asm__ __volatile__("rjmp __vectors"); /* reset! */

	return UDR0;
}

void serial_write(char c)
{
	/* Wait for empty transmit buffer */
	while ( !( UCSR0A & (1<<UDRE0)) );
	/* Put data into buffer, sends the data */
	UDR0 = c;
}

void serial_flushrx()
{
	unsigned char dummy;
	while ( UCSR0A & (1<<RXC0) )
		dummy = UDR0;
}
/*
	reset 1wire bus
	command mode
	rx 1wire reset command (no effect)
*/

/* ================= 1-wire bus low-level functions ========== */
#define uS(usec) ((usec##L) * CLK_FREQ) / 4L / 1000L

void wait(uint16_t delay)
{
    delay -= 2; // account for the time taken in the preceeding commands.

    // busy wait
    __asm__ __volatile__ (
        "1: sbiw %0,1" "\n\t" // 2 cycles
        "brne 1b" : "=w" (delay) : "0" (delay) // 2 cycles
    );
}


inline void bus_pull_down()
{
	OWPORT(DDR) |= (OWMASK);    // drives output low
}

inline void bus_release()
{
	OWPORT(DDR) &= ~(OWMASK);
}

inline uint8_t bus_read()
{
	return (OWPORT(PIN) & OWMASK) ? 1 : 0;
}

struct timing_t {
	/* Reset seq */
	uint16_t tRSTL; /* reset low time */
	uint16_t tSI; /* interrupt test delay */
	uint16_t tPDT; /* presence detection delay */
	uint16_t rFILL; /* response wait */
	/* Write-1 */
	uint16_t tLOW1; /* write-1 low length */
	uint16_t tDSO; /* write-1 sampling offset */
	uint16_t tHIGH1; /* write-1 high tail length */
	/* Write-0 */
	uint16_t tLOW0; /* write-0 low length */
	uint16_t tREC0; /* write-0 recovery time */
};
struct timing_t timings_standard  = { /* Standard/Flexible */
	uS(512), uS(8), uS(64), uS(512),  /* reset sequence: 584 uS total */
	uS(8), uS(3), uS(49),             /* timeslot: 60uS total */
	uS(57), uS(3),                    /* timeslot: 60uS total */
};
struct timing_t timings_overdrive = { /* Overdrive */
	uS(64),  uS(2), uS(8),  uS(64),   /* reset sequence: 74 uS total */
	uS(1), uS(1), uS(8),              /* timeslot: 10uS total */
	uS(7), uS(3),                     /* timeslot: 10uS total */
};
struct timing_t* timings = &timings_standard;
uint8_t search_accelerator_enabled = 0;

enum reset_result_t {
	RESET_SHORTED = 0x00,
	RESET_PRESENCE = 0x01,
	RESET_ALARM = 0x02,
	RESET_NOONE = 0x03,
};

enum reset_result_t bus_reset()
{
	bus_pull_down();
	wait(timings->tRSTL);
	bus_release();
	wait(timings->tSI);
	if(bus_read()) { /* normal */
		wait(timings->tPDT);
		return bus_read() ? RESET_NOONE : RESET_PRESENCE;
	} else { /* interrupt or short circuit */
		wait(uS(4096));
		return bus_read() ? RESET_ALARM : RESET_SHORTED;
	}
}
uint8_t bus_send_bit(uint8_t b)
{
	uint8_t r = b ? 1 : 0;
	bus_pull_down();
	if(b) {
		wait(timings->tLOW1);
		bus_release();
		wait(timings->tDSO);
		r = bus_read();
		wait(timings->tHIGH1);
	} else {
		wait(timings->tLOW0);
		bus_release();
		wait(timings->tREC0);
	}
	return r;
}
char bus_send_byte(char c)
{
	char r = 0;
	if(search_accelerator_enabled) { /* 4 search rom bits */
		for(uint8_t i = 0; i < 8; i += 2) {
			uint8_t b0 = bus_send_bit(1); /* read first */
			uint8_t b1 = bus_send_bit(1); /* read second */
			uint8_t b2 =
				(!(b0 | b1)/*conflict*/) ? c >> (i + 1) :
				(!(b0 & b1)/*1 response*/) ? b0 :
				1; /* no response */
			b2 = bus_send_bit(b2);
			uint8_t d = !(b0 ^ b1);
			d |= b2 << 1;
			r |= d << i;
		}
	} else { /*normal send byte */
		for(uint8_t i = 0; i < 8; ++i) {
			r |= bus_send_bit(c & 0x01) << i;
			c >>= 1;
		}
	}
	return r;
}
void bus_set_speed(uint8_t s) { }
void bus_strong_pullup() { }

union {
	uint8_t cf[8];
	struct { /* only lower 3 buts of each are used */
		uint8_t unused;
		uint8_t pdsrc;
		uint8_t ppd;
		uint8_t spud;
		uint8_t w1lt;
		uint8_t w0rt;
		uint8_t load;
		uint8_t rbr;
	} fields;
} conf;

void ds2480_update_conf(uint8_t index) { }

enum ds2480_mode_t {
	MODE_COMMAND,
	MODE_DATA,
	MODE_CHECK,
};

enum ds2480_mode_t ds2480_mode;

void execute_command(unsigned char c)
{
	char r;
	if(c & 0x80) { /* communication command */
		bus_set_speed((c >> 2) & 0x03);
		switch(c & 0xE1) {
		case 0x81: /* single bit */
			r = bus_send_bit(c & 0x10);
			r |= r << 1;
			r |= c & 0x9C;
			serial_write(r);
			if(c & 0x02) { /* strong pullup after time slot */
				bus_strong_pullup();
				serial_write((r & 0x03) | 0xEC);
			}
			break;
		case 0xA1: /* search accelerator control */
			search_accelerator_enabled = (c & 0x10) ? 1 : 0;
			break;
		case 0xC1: /* reset */
			r = bus_reset() | 0xCC;
			serial_write(r);
			break;
		case 0xE1: /* pulse */
			/* XXX */
			r = (c & 0x1C) | 0xE0;
			serial_write(r);
			break;
		default:
			break;
		}
	} else { /* configuration command */
		r = c & 0x70;
		if(r) { /* write param */
			ds2480_update_conf(r >> 4);
			conf.cf[r >> 4] = (c & 0x0E) >> 1;
			serial_write(c & 0xFE);
		} else { /* read param */
			r = conf.cf[(c & 0x0E) >> 1];
			serial_write((c & 0xF0) | (r << 1));
		}
	}
}

void echo();

int main()
{
	unsigned char c, c2;
	serial_init();
	serial_flushrx();
#if 0
	echo();
#endif
	PIO_PORT(PORT) = 0;
	bus_reset();
	serial_read_wait(); /* reset */
	ds2480_mode = MODE_COMMAND;
	for(;;) {
		c = serial_read_wait();
		switch(ds2480_mode) {
		case MODE_COMMAND:
			switch(c) {
			case 0xE1: /* switch to data mode */
				ds2480_mode = MODE_DATA;
				break;
			case 0xE3: /* switch to command mode */
			case 0xF1: /* pulse termination */
				break;
			default:
				execute_command(c);
				break;
			}
			break;
		case MODE_DATA:
			if(c == 0xE3) {
				c2 = c;
				ds2480_mode = MODE_CHECK;
			} else {
				serial_write(bus_send_byte(c));
			}
			break;
		case MODE_CHECK:
			if(c == c2)
				serial_write(bus_send_byte(c));
			else {
				ds2480_mode = MODE_COMMAND;
				execute_command(c);
			}
			break;
		}
	}
	return 0;
}

void echo()
{
	char c;
	for(;;) {
		c = serial_read_wait();
		serial_write(c);
		if(c == 0x0D)
			serial_write(0x0A);
	}
}



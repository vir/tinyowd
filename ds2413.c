#include "ows.h"
#include <avr/io.h>
#include <wdt.h>

char myrom[8] = {0x3A, 0xAA, 0xDA, 0xBB, 0xCF, 0x00, 0x00, 0x00};

void pio_send_state()
{
	/* |  7    6    5    4 |  3    2    1    0  |
	   |<complement of 3-0>|OutB PinB OutA PinA | */
	uint8_t sample = PIO_PORT(PIN) & 0x05;
	/* output values is inversion of direction register */
	sample |= (((~PIO_PORT(DDR)) & 0x05) << 1);
	sample |= (~sample << 4);
	ows_send(sample);
}

void pio_read()
{
	while(! errno)
		pio_send_state();
}

void pio_write()
{
	uint8_t data, cfm;
	while(! errno)
	{
		data = ows_recv();
		cfm = ~ows_recv();
		if(cfm != data)
			break;
		data = ~((data & 0x01) | ((data &0x02) << 1));
		data |= PIO_PORT(DDR) & ~0x05;
		PIO_PORT(DDR) = data;
		ows_send(0xAA);
		pio_send_state();
	}
}

void toggle_debug_led()
{
	DDRB|=0x08;
	PORTB^=0x08;
}

int main()
{
	wdt_disable();
#if defined(__AVR_ATtiny85__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny25__) || defined(__AVR_ATtiny13__)
	CLKPR = 0x80; /* Clock prescaler change enable */
	CLKPR = 0x00; /* Division Factor = 1, system clock 9.6MHz */
#endif
	ows_setup(myrom);
	PIO_PORT(PORT) = 0;
	for(;;)
		ows_wait_request();
}

void ows_process_cmds()
{
	switch(ows_recv())
	{
	case 0xF5: /* PIO ACCESS READ */
		pio_read();
		break;
	case 0x5A: /* PIO ACCESS WRITE */
		pio_write();
		break;
	default:
		break;
	}
}


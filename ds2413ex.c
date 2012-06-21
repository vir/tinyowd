#include "ows.h"
#include <avr/io.h>
#include "debounce.h"

/*
 * 0 - PioA
 * 2 - PioB
 * 3 - PioC
 * 4 - PioD
 */

static struct {
	uint8_t debouncer_mask;
	uint8_t int_mask;
} config = {
	0x00,
};

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

void pio_send_state2()
{
	/* |  7    6    5    4 |  3    2    1    0  |
	   |<complement of 3-0>|PinD PinC PinB PinA | */
	uint8_t sample = (PIO_PORT(PIN) & ~config.debouncer_mask) | (debounced_state() & config.debouncer_mask);
	sample &= 0x1D;
	sample = (sample >> 1) | (sample & 0x01);
	sample |= (~sample << 4);
	ows_send(sample);
}


inline void pio_read()
{
	while(! errno)
		pio_send_state();
}

inline void pio_read2()
{
	while(! errno)
		pio_send_state2();
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

ISR(TIM0_COMPA_vect) {
	uint8_t c = debounce(PIO_PORT(PIN));
#if 0 /* fix compilation temporary */
	if(c & config.int_mask)
		ows_interrupt();
#endif
	return 0;
}

int main()
{
	ows_setup2(0x3A, 0);
	PIO_PORT(PORT) = 0;
	for(;;)
	{
		if(ows_wait_request(0))
		{
#if 0 /* echo */
			while(! errno)
				ows_send(ows_recv());
			toggle_debug_led();
#else
			switch(ows_recv())
			{
			case 0xF5: /* PIO ACCESS READ */
				pio_read();
				break;
			case 0xFA: /* PIO ACCESS READ 2 */
				pio_read2();
				break;
			case 0x5A: /* PIO ACCESS WRITE */
				pio_write();
				break;
			default:
				break;
			}
#endif
		}
		else if(errno == ONEWIRE_INTERRUPTED)
		{
		}
	}
}



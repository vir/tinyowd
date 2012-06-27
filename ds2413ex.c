#include "ows.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include <wdt.h>
#include "debounce.h"
#include <string.h> /* for memcpy */
#include <avr/eeprom.h>

/*
 * 0 - PioA
 * 2 - PioB
 * 3 - PioC
 * 4 - PioD
 */

static struct {
	uint8_t debouncer_mask;
	uint8_t int_mask;
	uint8_t padding[6];
} config = {
	0x00,
	0x00,
};

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
	uint8_t sample = (PIO_PORT(PIN) & ~config.debouncer_mask) | (debounced_state & config.debouncer_mask);
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
}

int main()
{
	wdt_disable();
#if defined(__AVR_ATtiny85__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny25__) || defined(__AVR_ATtiny13__)
	CLKPR = 0x80; /* Clock prescaler change enable */
	CLKPR = 0x00; /* Division Factor = 1, system clock 9.6MHz */
#endif
	ows_setup2(0x3A, 0);
	eeprom_read_block(&config, (const void*)6, sizeof(config));
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
			case 0xBE: /* Read Scratchpad */
				ows_send_data((char*)&config, 8);
				ows_send(ows_crc8((char*)&config, 8));
				break;
			case 0x4E: /* Write Scratchpad */
				{
					char buf[9];
					if(sizeof(buf) == ows_recv_data(buf, sizeof(buf)) && buf[8] == ows_crc8(buf, 8))
						memcpy(&config, buf, sizeof(config));
				}
				break;
			case 0x48: /* Copy Scratchpad */
				eeprom_write_block(&config, (void*)6, sizeof(config));
				break;
			case 0xB8: /* Recall Scratchpad */
				eeprom_read_block(&config, (const void*)6, sizeof(config));
				break;
			default:
				break;
			}
#endif
		}
		else if(errno == ONEWIRE_INTERRUPTED)
		{
			int8_t diff = debounce(PIO_PORT(PIN));
#ifdef OWS_CONDSEARCH_ENABLE
			if(diff & config.int_mask)
				ows_set_flag(OWS_FLAG_CONDSEARCH | OWS_FLAG_INT_TYPE1 | OWS_FLAG_INT_TYPE2);
#endif
		}
	}
}



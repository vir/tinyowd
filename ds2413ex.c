#include "ows.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include <wdt.h>
#include "debounce.h"
#include <string.h> /* for memcpy */
#include <avr/eeprom.h>
#ifdef OWS_SPM_ENABLE
# include "ows_spm.h"
#endif

/*
 * b0 - PioA
 * b1 - PioB
 * b2 - PioC
 * b3 - PioD
 * b4 - 1wire bus
 */

static struct {
	uint8_t debouncer_mask;
	uint8_t int_mask;
	uint8_t int_type;
	uint8_t padding[5];
} config;

void pio_send_state()
{
	/* |  7    6    5    4 |  3    2    1    0  |
	   |<complement of 3-0>|OutB PinB OutA PinA | */
	uint8_t sample = PIO_PORT(PIN) & 0x03;
	sample = ((sample & 0x02) << 1) | (sample & 0x01);
	/* output values is inversion of direction register */
	sample |= ((PIO_PORT(DDR) & 0x02) << 2); /* OutB */
	sample |= ((PIO_PORT(DDR) & 0x01) << 1); /* OutA */
	sample |= (~sample << 4);
	ows_send(sample);
}

void pio_send_state2()
{
	/* |  7    6    5    4 |  3    2    1    0  |
	   |<complement of 3-0>|PinD PinC PinB PinA | */
	uint8_t sample = (PIO_PORT(PIN) & ~config.debouncer_mask) | (debounced_state & config.debouncer_mask);
	sample &= 0x0F;
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
		data = cfm & 0x03;
		data |= PIO_PORT(DDR) & ~0x03;
		PIO_PORT(DDR) = data;
		ows_send(0xAA);
		pio_send_state();
	}
}

void pio_write2()
{
	uint8_t data, cfm;
	while(! errno)
	{
	/* |  7    6    5    4 |  3    2    1    0  |
	   |  x    x    x    x |OutD OutC OutB OutA | */
		data = ows_recv();
		cfm = ~ows_recv();
		if(cfm != data)
			break;
		data = cfm & 0x0F;
		data |= PIO_PORT(DDR) & 0xF0;
		PIO_PORT(DDR) = data;
		ows_send(0xAA);
		pio_send_state2();
	}
}

ISR(TIM1_COMPA_vect) { /* occurs every 100uS */
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

	/* set up timer 1 */
	TCCR1 = 1<<CTC1 | 0x04; /* CTC mode, prescaler = 4 (clk/8) */
	GTCCR = 0;
	OCR1A = 0; // interrupt on 0
	OCR1C = CLK_FREQ / 80; /* 1/(9.6E6/8)*120=100uS period: OCR0A = 100 * CLK_FREQ / 1000 / 8 */
	TIMSK |= 1<<OCIE1A; /* interrupt on compare match */
	PLLCSR = 0;

#if 0
	for(;;)
	{
		if(ows_wait_request(0))
		{
			ows_process_cmds();
		}
		else if(errno == ONEWIRE_INTERRUPTED)
		{
			ows_process_interrupt();
		}
	}
#else
	for(;;)
		ows_wait_request();
#endif
}

void ows_process_cmds()
{
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
	case 0x5F: /* PIO ACCESS WRITE 2 */
		pio_write2();
		break;
	case 0x4E: /* Write Scratchpad */
		{
			char buf[sizeof(config) + 1];
			ows_recv_data(buf, sizeof(buf));
			if(buf[sizeof(config)] == ows_crc8(buf, sizeof(config)))
				memcpy(&config, buf, sizeof(config));
		}
		/* no break! */
	case 0xBE: /* Read Scratchpad */
		ows_send_data((char*)&config, 8);
			ows_send(ows_crc8((char*)&config, 8));
		break;
	case 0x48: /* Copy Scratchpad */
		eeprom_write_block(&config, (void*)6, sizeof(config));
		break;
	case 0xB8: /* Recall Scratchpad */
		eeprom_read_block(&config, (const void*)6, sizeof(config));
		break;
#ifdef OWS_SPM_ENABLE
	case 0xDA:
		ows_spm();
		break;
#endif
	default:
		break;
	}
}

void ows_process_interrupt()
{
	int8_t diff = debounce(PIO_PORT(PIN));
#ifdef OWS_CONDSEARCH_ENABLE
/*
                             ,----------- and --------+--> any
                             |         ,-/             > or ----- and --> interrupt
        l = int_mask(n)    --|-- not --+-- or -- not -+--> edge  /
        h = int_mask(n+4)  --+-- xor -----/                      |
        s = state(n)       -----/                                |
        d = diff           --------------------------------------'

*/
	int8_t s = debounced_state;
	int8_t h = config.int_mask >> 4;
	int8_t il = ~config.int_mask & 0x0F;
	if( ((il & h) | ~((s ^ h) | il)) & diff )
		ows_set_flag(OWS_FLAG_CONDSEARCH | (config.int_type & (OWS_FLAG_INT_TYPE1 | OWS_FLAG_INT_TYPE2)));
#endif
}


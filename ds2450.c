#include "ows.h"
#include "ow_crc16.h"
#include <avr/io.h>
#include <string.h>

struct {
	uint16_t conversion_readout[4]; // page 0
	struct {
		unsigned int rc:4; // number of bits, 0000 = 16
		unsigned int : 2;
		unsigned int oc:1; // output value
		unsigned int oe:1; // output enable
		unsigned int ir:1; // 0 - half max voltage, 1 - full max voltage
		unsigned int : 1;
		unsigned int ael:1; // alarm enable low
		unsigned int aeh:1; // alarm enable high
		unsigned int afl:1; // alarm flag low
		unsigned int afh:1; // alarm flag high
		unsigned int : 1;
		unsigned int por:1; // just powered on
	} control_status[4]; // page 1
	struct {
		uint8_t low;
		uint8_t high;
	} alarm_settings[4]; // page 2
	uint8_t calibration[8]; // page3
} memory;

void init_memory()
{
	memset(&memory, 0, sizeof(memory));
	uint16_t* p = (uint16_t*)&memory.control_status;
	*p++ = 0x8C08;
	*p++ = 0x8C08;
	*p++ = 0x8C08;
	*p++ = 0x8C08;
	*p++ = 0xFF00;
	*p++ = 0xFF00;
	*p++ = 0xFF00;
	*p++ = 0xFF00;
	memory.calibration[4] = 0x40;
}




char myrom[8] = {0x20, 0xBB, 0xAD, 0xCD, 0x0A, 0x00, 0x00, 0x00};


int main()
{
	init_memory();
	ows_setup(myrom);
	for(;;)
		ows_wait_request();
}

void ows_process_cmds()
{
	uint16_t memory_address;
	uint8_t b;
	ow_crc16_reset();
	switch(ows_recv())
	{
	case 0xAA: /* READ MEMORY */
		ow_crc16_update(0xAA);

		b = ows_recv();
		((uint8_t*)&memory_address)[0] = b;
		ow_crc16_update(b);

		b = ows_recv();
		((uint8_t*)&memory_address)[1] = b;
		ow_crc16_update(b);

		for(;;)
		{
			uint8_t b = ((uint8_t*)&memory)[memory_address];
			ows_send(b);
			ow_crc16_update(b);

			if(errno)
				break;

			if((memory_address & 0x0F) == 0x0F) /* end of page */
			{
				uint16_t crc = ow_crc16_get();
				ows_send(((uint8_t*)&crc)[0]);
				ows_send(((uint8_t*)&crc)[1]);
				ow_crc16_reset();
			}
			++memory_address;
			if(memory_address >= sizeof(memory))
				while(! errno)
					ows_send(0xFF);
		}
		break;
	case 0x55: /* WRITE MEMORY */
		ow_crc16_update(0x55);

		b = ows_recv();
		((uint8_t*)&memory_address)[0] = b;
		ow_crc16_update(b);

		b = ows_recv();
		((uint8_t*)&memory_address)[1] = b;
		ow_crc16_update(b);

		for(;;)
		{
			b = ows_recv();
			if(errno)
				break;
			ow_crc16_update(b);
			uint16_t crc = ow_crc16_get();
			ows_send(((uint8_t*)&crc)[0]);
			ows_send(((uint8_t*)&crc)[1]);

			((uint8_t*)&memory)[memory_address] = b;

			ows_send(b);
			if(errno)
				break;

			++memory_address;
			ow_crc16_reset();
			ow_crc16_update(((uint8_t*)&memory_address)[0]);
			ow_crc16_update(((uint8_t*)&memory_address)[1]);
		}

		break;
	case 0x3C: /* CONVERT */
		break;
	default:
		break;
	}
}

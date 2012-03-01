#include "ows.h"
#include <avr/io.h>

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
	{
		if(ows_wait_request(0))
		{
			switch(ows_recv())
			{
			case 0xAA: /* READ MEMORY */
				break;
			case 0x55: /* WRITE MEMORY */
				break;
			case 0x3C: /* CONVERT */
				break;
			default:
				break;
			}
		}
	}
}



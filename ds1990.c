#include "ows.h"

char myrom[8] = {0x01, 0xAD, 0xDA, 0xCE, 0x0F, 0x00, 0x00, 0x00};

int main()
{
	ows_setup(myrom);
	for(;;)
		ows_wait_request(0);
}



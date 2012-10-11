#include "ows.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include <wdt.h>

int main()
{
	wdt_disable();
#if defined(__AVR_ATtiny85__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny25__) || defined(__AVR_ATtiny13__)
	CLKPR = 0x80; /* Clock prescaler change enable */
	CLKPR = 0x00; /* Division Factor = 1, system clock 9.6MHz */
#endif
	ows_setup2(0x3A, 0);
	PIO_PORT(PORT) = 0;
	for(;;)
	{
		ows_wait_request(0);
	}
}



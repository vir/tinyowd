#include "ows_spm.h"
#include "ows.h"
#include "ow_crc16.h"
#include <avr/io.h>
#include <avr/boot.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>

#define PGM_PAGE_SIZE 32

void boot_program_page(uint32_t page, uint8_t *buf);

static void write_page(uint16_t addr)
{
	uint8_t sreg;
	sreg = SREG;
	cli();
	eeprom_busy_wait();
	boot_page_erase(addr);
	boot_spm_busy_wait();
	boot_page_write(addr);
	boot_spm_busy_wait();
	SREG = sreg;
}

void ows_spm()
{
	uint8_t cmd = ows_recv();
	uint16_t addr = ows_recv() << 8;
	addr |= ows_recv();
	switch(cmd) {
	case 0x33: /* read program memory page */
		ow_crc16_reset();
		for(uint8_t i = 0; i < PGM_PAGE_SIZE; ++i) {
			uint8_t c = pgm_read_byte_near(addr++);
			ows_send(c);
			ow_crc16_update(c);
		}
		addr = ow_crc16_get();
		ows_send(addr >> 8);
		ows_send(addr & 0xFF);
		break;
	case 0x3C: /* fill program memory page write buffer, return crc */
		ow_crc16_reset();
		for(uint8_t i = 0; i < PGM_PAGE_SIZE; i += 2) {
			uint16_t w;
			uint8_t c = ows_recv();
			ow_crc16_update(c);
			w = c << 8;
			c = ows_recv();
			w |= c;
			boot_page_fill(addr + i, w);
		}
		addr = ow_crc16_get();
		ows_send(addr >> 8);
		ows_send(addr & 0xFF);
		break;
	case 0x5A: /* write page buffer */
		write_page(addr);
		ows_send(0);
		ows_send(0);
		ows_send(0);
		break;
	case 0xA5: /* read eeprom page */
		break;
	case 0xAA: /* write eeprom page */
		break;
	case 0xC3: /* jump to address */
		break;
	case 0xCC: /* read device id and page size */
		break;
	}
}

void boot_program_page(uint32_t page, uint8_t *buf)
{

	uint16_t i;
	uint8_t sreg;

	// Disable interrupts.

	sreg = SREG;
	cli();

	eeprom_busy_wait ();

	boot_page_erase (page);
	boot_spm_busy_wait ();      // Wait until the memory is erased.

	for (i=0; i<SPM_PAGESIZE; i+=2)
	{
		// Set up little-endian word.
		uint16_t w = *buf++;
		w += (*buf++) << 8;
		boot_page_fill (page + i, w);
	}

	boot_page_write (page);     // Store buffer in flash page.
	boot_spm_busy_wait();       // Wait until the memory is written.

	// Reenable RWW-section again. We need this if we want to jump back
	// to the application after bootloading.
	//boot_rww_enable ();

	// Re-enable interrupts (if they were ever enabled).
	SREG = sreg;
}



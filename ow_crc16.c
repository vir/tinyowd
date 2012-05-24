
/*
  Dallas 1-wire CRC routines. Based on ardino routines by Kairama Inc
  from http://www.arduino.cc/cgi-bin/yabb2/YaBB.pl?num=1279505626

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdint.h>

static uint16_t crc16;

void ow_crc16_reset()
{
	crc16 = 0;
}

void ow_crc16_update(uint8_t b)
{
	for (uint8_t j=0;j<8;j++)
	{
		uint8_t mix = ((uint8_t)crc16 ^ b) & 0x01;
		crc16 = crc16 >> 1;
		if (mix)
			crc16 = crc16 ^ 0xA001;

		b = b >> 1;
	}
}

uint16_t ow_crc16_get()
{
	return crc16;
}


/*
 * crc.c
 *
 *  Created on: Jul 17, 2025
 *      Author: shogl
 */

#include <crc.h>

/* uint8_t crc8(const void* data, uint8_t length) {
    uint8_t crc = 0; // <-- add this line
    const uint8_t* p = (const uint8_t*)data;
    while (length--) {
        uint8_t b = *p++, j = 8;
        while (j--) {
            crc = ((crc ^ b) & 1) ? (crc >> 1) ^ 0x8C : (crc >> 1);
            b >>= 1;
        }
    }
    return crc;
} */

uint8_t Calculate_CRC8 (const uint8_t *data, uint16_t length){
	uint8_t crc = 0x00;
	for (uint16_t i = 0; i < length; i++)
	{
		crc ^= data[i];
		for (uint8_t j = 0; j < 8; j++)
		{
			if (crc & 0x80)
			{
				crc = (crc << 1) ^ 0x07;
			}
			else
			{
				crc <<= 1;
			}
		}
	}
	return crc;
}

#include "format40.hpp"

/**
 * Decode a memory fragment which is encoded with 'format40'.
 * @param dst The place the decoded fragment will be loaded.
 * @param src The encoded fragment.
 */
void Format40_Decode(uint8_t *dst, uint8_t *src)
{
	while (true) {
		uint16_t flag;

		flag = *src++;

		if (flag == 0) {
			flag = *src++;
			for (; flag > 0; flag--) {
				*dst++ ^= *src;
			}
			src++;

			continue;
		}

		if ((flag & 0x80) == 0) {
			for (; flag > 0; flag--) {
				*dst++ ^= *src++;
			}
			continue;
		}

		if (flag != 0x80) {
			dst += flag & 0x7F;
			continue;
		}

		flag = *src++;
		flag += (*src++) << 8;

		if (flag == 0) break;

		if ((flag & 0x8000) == 0) {
			dst += flag;
			continue;
		}

		if ((flag & 0x4000) == 0) {
			flag &= 0x3FFF;
			for (; flag > 0; flag--) {
				*dst++ ^= *src++;
			}
			continue;
		}

		{
			flag &= 0x3FFF;
			for (; flag > 0; flag--) {
				*dst++ ^= *src;
			}
			src++;
			continue;
		}
	}
}

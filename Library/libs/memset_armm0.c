/*
 * memset for the 32-bit ARM port - see memcpy_armm0.c for why.
 *
 * Zeroing a LOCAL frame is the hottest memset in an MMBasic program:
 * one per call to any SUB with LOCAL strings, 257 bytes per string,
 * and at the generic byte loop's nine cycles a byte that was 6.6us of
 * a 7.4us call.
 */

#include <string.h>
#include <stdint.h>

void *memset(void *dest, int data, size_t len)
{
	unsigned char *p = dest;
	unsigned char v = (unsigned char)data;

	while (len && ((uintptr_t)p & 3)) {
		*p++ = v;
		len--;
	}
	{
		uint32_t w = (uint32_t)v;
		uint32_t *pw = (uint32_t *)(void *)p;

		w |= w << 8;
		w |= w << 16;
		while (len >= 16) {
			pw[0] = w;
			pw[1] = w;
			pw[2] = w;
			pw[3] = w;
			pw += 4;
			len -= 16;
		}
		while (len >= 4) {
			*pw++ = w;
			len -= 4;
		}
		p = (unsigned char *)(void *)pw;
	}
	while (len--)
		*p++ = v;
	return dest;
}

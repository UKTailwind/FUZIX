/*
 * memcpy for the 32-bit ARM port.
 *
 * The generic memcpy.c is a byte loop, which is the right answer on an
 * 8-bit machine and costs about nine cycles a byte here.  It showed up
 * as MMBasic string work: a SUB with two LOCAL strings spent 12us per
 * call moving and zeroing 514 bytes of frame, more than the whole rest
 * of the call put together.
 *
 * Words when both ends are aligned the same way, bytes otherwise: this
 * platform runs with unaligned access trapping (an unaligned STRD is
 * what found that), so the alignment test is not optional.  Four words
 * at a time in the main loop, which gcc turns into ldm/stm pairs.
 */

#include <string.h>
#include <stdint.h>

void *memcpy(void *dest, const void *src, size_t len)
{
	unsigned char *d = dest;
	const unsigned char *s = src;

	if (((uintptr_t)d ^ (uintptr_t)s) & 3) {
		/* different alignments - words would fault, so bytes */
		while (len--)
			*d++ = *s++;
		return dest;
	}
	while (len && ((uintptr_t)d & 3)) {
		*d++ = *s++;
		len--;
	}
	{
		uint32_t *dw = (uint32_t *)(void *)d;
		const uint32_t *sw = (const uint32_t *)(const void *)s;

		while (len >= 16) {
			dw[0] = sw[0];
			dw[1] = sw[1];
			dw[2] = sw[2];
			dw[3] = sw[3];
			dw += 4;
			sw += 4;
			len -= 16;
		}
		while (len >= 4) {
			*dw++ = *sw++;
			len -= 4;
		}
		d = (unsigned char *)(void *)dw;
		s = (const unsigned char *)(const void *)sw;
	}
	while (len--)
		*d++ = *s++;
	return dest;
}

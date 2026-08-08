/*
 * memcpy/memset for the 32-bit ARM port, against a byte-loop oracle.
 *
 * The word-wise versions have a head, a body and a tail, and take a
 * different path when source and destination are aligned differently -
 * so the cases that matter are every length across the block sizes and
 * every combination of the two alignments, with the bytes either side
 * of the region checked for damage.
 */

#include <stdio.h>
#include <string.h>

#define PAD	8
#define MAX	80

static unsigned char dst[MAX + 2 * PAD + 8];
static unsigned char src[MAX + 2 * PAD + 8];
static unsigned char ref[MAX + 2 * PAD + 8];

static int fails, tests;

static void refcpy(unsigned char *d, const unsigned char *s, int n)
{
	while (n--)
		*d++ = *s++;
}

static void refset(unsigned char *d, int v, int n)
{
	while (n--)
		*d++ = (unsigned char)v;
}

static void check(const char *what, int da, int sa, int len)
{
	int i;

	tests++;
	for (i = 0; i < (int)sizeof dst; i++) {
		dst[i] = ref[i] = (unsigned char)(0xA0 + (i & 15));
		src[i] = (unsigned char)(i * 7 + 3);
	}
	if (what[3] == 'c') {			/* memcpy */
		memcpy(dst + PAD + da, src + PAD + sa, (size_t)len);
		refcpy(ref + PAD + da, src + PAD + sa, len);
	} else {				/* memset */
		memset(dst + PAD + da, 0x5A, (size_t)len);
		refset(ref + PAD + da, 0x5A, len);
	}
	for (i = 0; i < (int)sizeof dst; i++)
		if (dst[i] != ref[i]) {
			fails++;
			printf("FAIL %s da=%d sa=%d len=%d at %d: %02x != %02x\n",
			       what, da, sa, len, i, dst[i], ref[i]);
			return;
		}
}

int main(void)
{
	int da, sa, len;

	for (da = 0; da < 4; da++)
		for (sa = 0; sa < 4; sa++)
			for (len = 0; len <= MAX; len++)
				check("memcpy", da, sa, len);
	for (da = 0; da < 4; da++)
		for (len = 0; len <= MAX; len++)
			check("memset", da, 0, len);
	printf("%d cases, %d failures\n", tests, fails);
	return fails != 0;
}

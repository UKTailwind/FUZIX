/*
 * Do string slots in the image beat strings in the PSRAM heap for a
 * REAL program - one whose other data keeps evicting them?
 *
 * PSRAM is around 2.5x slower than SRAM when the access misses, but the
 * XIP cache sits in front of it, and a few string variables are small
 * enough to live there: measured alone (srampsram.c) the two memories
 * are indistinguishable, 1.00x at every size up to 16K.  So the only
 * case where moving strings into the process image can pay is when
 * something else evicts them between touches - arrays, a framebuffer,
 * the kernel's own code running from flash through the same cache.
 *
 * Each round here does the same string work either way, and between
 * rounds sweeps an array of PRESSURE bytes, which is what a graphics
 * or array program is doing anyway.  Strings in the image cannot be
 * evicted by it; strings in the heap can.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

long long time_us64(void);

#define STRSZ	258
#define NSTR	16			/* 4K of strings: a normal program */
#define PRESSURE 262144UL		/* 256K swept between rounds */

static unsigned char image[NSTR][STRSZ];

static void fill(unsigned char (*a)[STRSZ])
{
	int i, j;

	for (i = 0; i < NSTR; i++) {
		a[i][0] = 100;
		for (j = 1; j <= 100; j++)
			a[i][j] = (unsigned char)('a' + ((i + j) & 15));
		a[i][101] = 0;
	}
}

static long strwork(unsigned char (*a)[STRSZ], int reps)
{
	int i, k;
	long sum = 0;

	for (k = 0; k < reps; k++)
		for (i = 0; i < NSTR - 1; i++) {
			int la = a[i][0];
			int lb = a[i + 1][0];
			int m = la + lb;

			if (m > 255)
				m = 255;
			memcpy(a[i] + 1 + la, a[i + 1] + 1, (size_t)(m - la));
			a[i][0] = (unsigned char)m;
			sum += memcmp(a[i] + 1, a[i + 1] + 1, 32);
			a[i][0] = (unsigned char)la;
		}
	return sum;
}

/* the rest of the program's working set, swept once per round */
static long sweep(unsigned char *p)
{
	unsigned long j;
	long sum = 0;

	for (j = 0; j < PRESSURE; j += 32)
		sum += p[j];
	return sum;
}

static long long timed(unsigned char (*s)[STRSZ], unsigned char *press,
		       int rounds, int reps, int pressure)
{
	long long t0;
	long sink = 0;
	int r;

	fill(s);
	t0 = time_us64();
	for (r = 0; r < rounds; r++) {
		sink += strwork(s, reps);
		if (pressure)
			sink += sweep(press);
	}
	t0 = time_us64() - t0;
	if (sink == 12345678L)
		printf("");
	return t0;
}

int main(void)
{
	unsigned char (*heap)[STRSZ];
	unsigned char *press;
	long long si, sh, pi, ph;

	heap = malloc((unsigned long)NSTR * STRSZ);
	press = malloc(PRESSURE);
	if (!heap || !press) {
		printf("no heap\n");
		return 1;
	}
	memset(press, 1, PRESSURE);
	printf("strings: image %lx  heap %lx   pressure %lx\n",
	       (unsigned long)image, (unsigned long)heap,
	       (unsigned long)press);

	/* warm both ways round first, then measure */
	timed(image, press, 20, 200, 0);
	timed(heap, press, 20, 200, 0);
	si = timed(image, press, 200, 200, 0);
	sh = timed(heap, press, 200, 200, 0);
	printf("quiet     image %8ld us   heap %8ld us\n", (long)si, (long)sh);

	timed(image, press, 20, 200, 1);
	timed(heap, press, 20, 200, 1);
	pi = timed(image, press, 200, 200, 1);
	ph = timed(heap, press, 200, 200, 1);
	printf("pressure  image %8ld us   heap %8ld us\n", (long)pi, (long)ph);
	printf("the sweep alone costs about %ld us of that\n",
	       (long)(pi - si));
	return 0;
}

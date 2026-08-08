/*
 * SRAM against PSRAM, for string-shaped work.
 *
 * Under bcrun a static array lives in mem[], which is the process image
 * and so SRAM; malloc comes from the VM heap, which the kernel carves
 * out of PSRAM (bcrun.c heap_init).  MMBasic's strings and arrays are
 * all malloc'd today, so this is the gap a program pays for every
 * string operation - and the gap a pool of string slots in the image
 * would close.
 *
 * PSRAM is reached through the QMI with the XIP cache in front of it,
 * so the answer depends on the working set: a few string variables may
 * sit in the cache and cost nothing, while an array that does not fit
 * pays the miss on every touch.  Hence three sizes, and the addresses
 * printed so there is no doubt which memory is which.
 *
 * time_us64 is a bcrun native (bcrun.c lc_time_us64) - microseconds,
 * where time() would only have given whole seconds.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

long long time_us64(void);

#define STRSZ	258
#define BIGN	256			/* 256 * 258 = 64.5K */

static unsigned char image[BIGN][STRSZ];

/* One pass of MMBasic-shaped string work over n slots: copy, append,
   compare - no allocation inside the loop.  Returns a checksum so
   nothing folds away. */
static long work(unsigned char (*a)[STRSZ], int n, int reps)
{
	int i, k;
	long sum = 0;

	for (k = 0; k < reps; k++) {
		for (i = 0; i < n - 1; i++) {
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
	}
	return sum;
}

static void fill(unsigned char (*a)[STRSZ], int n)
{
	int i, j;

	for (i = 0; i < n; i++) {
		a[i][0] = 100;
		for (j = 1; j <= 100; j++)
			a[i][j] = (unsigned char)('a' + ((i + j) & 15));
		a[i][101] = 0;
	}
}

static long long timed(unsigned char (*a)[STRSZ], int n, int reps)
{
	long long t0, t1;

	fill(a, n);
	t0 = time_us64();
	work(a, n, reps);
	t1 = time_us64();
	return t1 - t0;
}

static void pair(const char *label, unsigned char (*heap)[STRSZ],
		 int n, int reps)
{
	long long s, p;

	/* interleaved, and the first of each thrown away: the cache
	   state left by the other run is the thing being measured */
	timed(image, n, reps);
	timed(heap, n, reps);
	s = timed(image, n, reps);
	p = timed(heap, n, reps);
	printf("%-10s SRAM %8ld us   PSRAM %8ld us   x%ld.%02ld\n",
	       label, (long)s, (long)p,
	       (long)(p / (s ? s : 1)),
	       (long)((p * 100 / (s ? s : 1)) % 100));
}

int main(void)
{
	unsigned char (*heap)[STRSZ];

	heap = malloc((unsigned long)BIGN * STRSZ);
	if (!heap) {
		printf("no heap\n");
		return 1;
	}
	printf("image %lx  heap %lx  (SRAM is 2xxxxxxx)\n",
	       (unsigned long)image, (unsigned long)heap);
	pair("4K/16sl",   heap, 16,  2000);
	pair("16K/64sl",  heap, 64,  500);
	pair("64K/256sl", heap, 256, 125);
	return 0;
}

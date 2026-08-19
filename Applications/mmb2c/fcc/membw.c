/*
 * What PSRAM actually costs against SRAM on this board.
 *
 * The string-shaped test (srampsram.c) found no difference, but it was
 * not memory bound: per slot it moves ~100 bytes under the VM's own
 * loop overhead, and its working set fits behind the XIP cache that
 * sits in front of PSRAM.  This one measures the memory:
 *
 *   copy     - block memcpy, the native slot, so almost no VM overhead
 *   stride   - one byte read per 64, which defeats the prefetcher and
 *              exposes miss latency rather than bandwidth
 *
 * and it runs each at a size that fits the cache and a size that
 * cannot, so the cache's contribution is visible rather than assumed.
 * Only PSRAM can hold the large case - the image is part of bcrun's
 * own process - so the honest comparison there is PSRAM against
 * itself: cached against not.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

long long time_us64(void);

#define IMGSZ	8192			/* two of these is all the image
					   will take before bcrun says
					   "program too large" */
static unsigned char ia[IMGSZ], ib[IMGSZ];

static long long copy_test(unsigned char *d, unsigned char *s,
			   unsigned long sz, int reps)
{
	long long t0;
	int i;

	memset(s, 1, (size_t)sz);
	t0 = time_us64();
	for (i = 0; i < reps; i++)
		memcpy(d, s, (size_t)sz);
	return time_us64() - t0;
}

static long long stride_test(unsigned char *p, unsigned long sz, int reps)
{
	long long t0;
	unsigned long j;
	int i;
	long sum = 0;

	memset(p, 1, (size_t)sz);
	t0 = time_us64();
	for (i = 0; i < reps; i++)
		for (j = 0; j < sz; j += 64)
			sum += p[j];
	t0 = time_us64() - t0;
	if (sum == 0)
		printf("");		/* keep it */
	return t0;
}

int main(void)
{
	unsigned char *ha, *hb;
	unsigned long big = 262144UL;
	long long a, b;

	ha = malloc(big);
	hb = malloc(big);
	if (!ha || !hb) {
		printf("no heap\n");
		return 1;
	}
	printf("image %lx %lx   heap %lx %lx\n",
	       (unsigned long)ia, (unsigned long)ib,
	       (unsigned long)ha, (unsigned long)hb);

	/* 8K: fits any cache, so this is the fair like-for-like */
	a = copy_test(ib, ia, IMGSZ, 2000);
	b = copy_test(hb, ha, IMGSZ, 2000);
	printf("copy   8K   SRAM %7ld us   PSRAM %7ld us\n", (long)a, (long)b);

	a = stride_test(ia, IMGSZ, 2000);
	b = stride_test(ha, IMGSZ, 2000);
	printf("stride 8K   SRAM %7ld us   PSRAM %7ld us\n", (long)a, (long)b);

	/* 256K: PSRAM against itself, cached against not */
	b = copy_test(hb, ha, big, 62);
	printf("copy   256K            PSRAM %7ld us  (same bytes moved)\n",
	       (long)b);
	b = stride_test(ha, big, 62);
	printf("stride 256K            PSRAM %7ld us  (same reads)\n",
	       (long)b);
	return 0;
}

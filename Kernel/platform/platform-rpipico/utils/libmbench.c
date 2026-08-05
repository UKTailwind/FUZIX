/*
 * libmbench - is the kernel's shared libm fast enough to be worth it?
 *
 * The kernel now exports one copy of libm from flash (libm_table.c) so
 * that every program can call it instead of linking its own 13K.  That
 * only pays if calling it is not appreciably slower, and it might be:
 * the kernel's copy executes from XIP flash through a 16K cache that
 * the display and everything else are also using, while a program's
 * own copy is in RAM.
 *
 * So: the same work twice, through the table and through our own libm,
 * and print both.  Nothing is reworked until this says it is safe.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "../pico_ioctl.h"

#define N 20000

static double sink;

int main(void)
{
	const struct pc3_libm *t;
	void *p = 0;
	double (*k_sin)(double), (*k_cos)(double);
	double (*k_pow)(double, double);
	int fd, i;
	clock_t a, b, own, shared;
	double x;

	fd = open("/dev/sys", O_RDWR, 0);
	if (fd < 0) {
		perror("/dev/sys");
		return 1;
	}
	if (ioctl(fd, PICOIOC_LIBM, &p) < 0) {
		perror("PICOIOC_LIBM");
		return 1;
	}
	t = (const struct pc3_libm *)p;
	printf("table at %p  magic %08lx  version %u  count %u\n",
	       p, (unsigned long)t->magic, t->version, t->count);
	if (t->magic != PC3_LIBM_MAGIC || t->version != PC3_LIBM_VERSION) {
		fprintf(stderr, "not the table this was built against\n");
		return 1;
	}

	k_sin = (double (*)(double))t->fn[PC3_LIBM_SIN];
	k_cos = (double (*)(double))t->fn[PC3_LIBM_COS];
	k_pow = (double (*)(double, double))t->fn[PC3_LIBM_POW];

	/* correctness before speed - a wrong ABI would show here as
	   nonsense rather than as a crash */
	printf("shared: sin(1)=%.9f cos(1)=%.9f pow(2,10)=%.1f\n",
	       k_sin(1.0), k_cos(1.0), k_pow(2.0, 10.0));
	printf("own:    sin(1)=%.9f cos(1)=%.9f pow(2,10)=%.1f\n",
	       sin(1.0), cos(1.0), pow(2.0, 10.0));

	a = clock();
	for (i = 0, x = 0.0; i < N; i++, x += 0.0001)
		sink += sin(x) + cos(x);
	b = clock();
	own = b - a;

	a = clock();
	for (i = 0, x = 0.0; i < N; i++, x += 0.0001)
		sink += k_sin(x) + k_cos(x);
	b = clock();
	shared = b - a;

	printf("%d sin+cos: own %ld ticks, shared %ld ticks\n",
	       N, (long)own, (long)shared);
	if (own)
		printf("shared is %ld%% of own\n", (long)(shared * 100 / own));
	return 0;
}

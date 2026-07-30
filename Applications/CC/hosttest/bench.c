/*
 *	bench.c - the interpreter-vs-native baseline for the Thumb
 *	backend.
 *
 *	The eclipse is a poor yardstick for code generation: it lives in
 *	native libm, so the dispatch overhead the backend removes is a
 *	minority of its time.  These phases are what bytecode dispatch
 *	actually costs: integer, call and memory bound, no floating
 *	point in any hot loop, no library call inside one.
 *
 *	Every phase prints a 32-bit checksum as well as its time, so a
 *	native gcc build and a bcrun run are compared for correctness,
 *	not just speed.  Checksums use "unsigned", which is 32 bits on
 *	both the host and the target - "long" is not.
 *
 *	Compiled two ways by bench.sh: FCC to bytecode (-DMM_FCC, where
 *	time_us binds as a bcrun libcall) and gcc -O2 native.
 */

#include <stdio.h>

#ifdef MM_FCC
extern long time_us();
static long now_us(void) { return time_us(); }
#else
#include <sys/time.h>
static long now_us(void)
{
	struct timeval tv;
	gettimeofday(&tv, 0);
	return (long)(tv.tv_sec * 1000000L + tv.tv_usec);
}
#endif

static long t0;
static void tick(void) { t0 = now_us(); }
static void tock(const char *name, unsigned check)
{
	long ms = (now_us() - t0) / 1000;
	printf("%s: %ld ms  check %u\n", name, ms, check);
}

/* ---- sieve: byte array, branches, inner strides -------------------- */

#define SIEVE_N		8191
#define SIEVE_IT	10
static char flags[SIEVE_N + 1];

static unsigned sieve(void)
{
	int i, k, it;
	unsigned count = 0;

	for (it = 0; it < SIEVE_IT; it++) {
		count = 0;
		for (i = 0; i <= SIEVE_N; i++)
			flags[i] = 1;
		for (i = 2; i <= SIEVE_N; i++) {
			if (flags[i]) {
				for (k = i + i; k <= SIEVE_N; k += i)
					flags[k] = 0;
				count++;
			}
		}
	}
	return count;
}

/* ---- fib: call, return, argument and stack traffic ----------------- */

static int fib(int n)
{
	if (n < 2)
		return n;
	return fib(n - 1) + fib(n - 2);
}

/* ---- shellsort: indexed loads/stores and compares ------------------ */

#define SORT_N	2000
#define SORT_IT	5
static int sorted[SORT_N];

static unsigned shellsort(void)
{
	int gap, i, j, tv, it;
	unsigned seed, sum = 0;

	for (it = 0; it < SORT_IT; it++) {
		seed = 12345;
		for (i = 0; i < SORT_N; i++) {
			seed = seed * 1103515245u + 12345u;
			sorted[i] = (int)(seed >> 16) & 0x7FFF;
		}
		for (gap = SORT_N / 2; gap > 0; gap /= 2)
			for (i = gap; i < SORT_N; i++) {
				tv = sorted[i];
				for (j = i; j >= gap && sorted[j - gap] > tv; j -= gap)
					sorted[j] = sorted[j - gap];
				sorted[j] = tv;
			}
		sum += (unsigned)sorted[0] + (unsigned)sorted[SORT_N / 2]
		     + (unsigned)sorted[SORT_N - 1];
	}
	return sum;
}

/* ---- xorshift: pure 32-bit register arithmetic --------------------- */

#define RNG_IT	200000

static unsigned rngsum(void)
{
	unsigned x = 2463534242u, sum = 0;
	long i;

	for (i = 0; i < RNG_IT; i++) {
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		sum += x;
	}
	return sum;
}

/* ---- byte reversal: tight loads and stores ------------------------- */

#define BUF_N	4096
#define REV_IT	200
static char buf[BUF_N];

static unsigned revsum(void)
{
	int it, i;
	char t;
	unsigned sum = 0;

	for (i = 0; i < BUF_N; i++)
		buf[i] = (char)(i * 7 + 3);
	for (it = 0; it < REV_IT; it++) {
		for (i = 0; i < BUF_N / 2; i++) {
			t = buf[i];
			buf[i] = buf[BUF_N - 1 - i];
			buf[BUF_N - 1 - i] = t;
		}
		sum += (unsigned char)buf[it & (BUF_N - 1)];
	}
	return sum;
}

int main(void)
{
	tick(); tock("startup", 0);

	tick(); tock("sieve  ", sieve());
	tick(); tock("fib(27)", (unsigned)fib(27));
	tick(); tock("sort   ", shellsort());
	tick(); tock("rng    ", rngsum());
	tick(); tock("rev    ", revsum());
	return 0;
}

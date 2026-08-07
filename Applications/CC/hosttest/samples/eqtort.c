/*
 * Compound-assign torture: every base at every width, pre and post,
 * against gcc as the oracle (optest.sh) and 3-way under qemu.  The
 * arguments defeat constant folding; the loop makes the int and the
 * long long counters hot enough for the register caches to pick
 * them, so the r7 and (under THUMB_REGC8) r8:r9 eqop paths are the
 * ones exercised, not just the memory inlines.
 *
 * Deliberately absent: division and remainder by zero (undefined in
 * C; native follows the hardware as gcc does, the interpreter guards
 * to zero - a known, documented divergence on UB, not a defect).
 */
int printf();

void pll(char *tag, long long v)
{
	/* bcrun printf has no %lld: print the halves.  Masked BOTH
	   ways: the oracle's unsigned long is 64-bit and would print
	   the whole value where the target prints the low half. */
	unsigned long hi = (unsigned long)
	    (((unsigned long long)v >> 32) & 0xFFFFFFFFul);
	unsigned long lo = (unsigned long)(v & 0xFFFFFFFFul);

	printf("%s %lu %lu\n", tag, hi, lo);
}

int tint(int a, int b)
{
	int x = a;
	int i;
	int acc = 0;

	for (i = 0; i < 8; i += 1) {
		x += b; x -= 3; x *= 2; x /= 3;
		x %= 100000; x &= 0xFFFFF7; x |= 9; x ^= 0x1234;
		x <<= 2; x >>= 1;
		acc += x++;
		acc += ++x;
		acc += x--;
		acc += --x;
	}
	printf("int %d %d\n", x, acc);
	return x + acc;
}

unsigned tuns(unsigned a, unsigned b)
{
	unsigned x = a;
	unsigned acc = 0;
	int i;

	for (i = 0; i < 8; i += 1) {
		x += b; x -= 7; x *= 3; x /= 5;
		x %= 1000000; x &= 0xFFFFFF; x |= 3; x ^= 0x4321;
		x <<= 3; x >>= 2;
		acc += x++;
		acc += --x;
	}
	printf("uns %u %u\n", x, acc);
	return x + acc;
}

long long tll(long long a, long long b)
{
	long long x = a;
	long long acc = 0;
	long long i;

	for (i = 0; i < 8; i += 1) {
		x += b; x -= 123456789LL;
		x &= 0xFFFFFFFFFFFFLL; x |= 0x101; x ^= 0x77777;
		acc += x++;
		acc += ++x;
		acc += x--;
		acc += --x;
		/* the helper-path kinds too */
		x *= 3; x /= 7; x %= 1000000007LL;
		x <<= 5; x >>= 3;
	}
	pll("llx", x);
	pll("lla", acc);
	return x + acc;
}

unsigned long long tull(unsigned long long a, unsigned long long b)
{
	unsigned long long x = a;
	unsigned long long acc = 0;
	int i;

	for (i = 0; i < 4; i += 1) {
		x += b; x -= 99999999ULL;
		x &= 0xFFFFFFFFFFFFFFULL; x |= 0x11; x ^= 0xABCDE;
		x *= 5; x /= 9; x %= 998244353ULL;
		x <<= 7; x >>= 6;
		acc += x++;
		acc += --x;
	}
	pll("ullx", (long long)x);
	pll("ulla", (long long)acc);
	return x + acc;
}

int tnarrow(int a)
{
	signed char c = (signed char)a;
	unsigned char uc = (unsigned char)a;
	short s = (short)(a * 3);
	unsigned short us = (unsigned short)(a * 5);

	c += 7; c -= 2; c *= 3; c /= 2; c %= 23;
	c &= 0x5F; c |= 6; c ^= 0x11; c <<= 1; c >>= 1;
	c++; ++c; c--; --c;
	uc += 91; uc -= 3; uc *= 5; uc /= 3; uc %= 200;
	uc &= 0xEF; uc |= 0x21; uc ^= 0x40; uc <<= 2; uc >>= 1;
	uc++; --uc;
	s += 1000; s -= 77; s *= 3; s /= 5; s %= 9999;
	s &= 0x7EFF; s |= 0x101; s ^= 0x2222; s <<= 1; s >>= 2;
	s++; --s;
	us += 40000; us -= 123; us *= 7; us /= 6; us %= 50000;
	us &= 0xFEFF; us |= 0x88; us ^= 0x4444; us <<= 2; us >>= 1;
	us--; ++us;
	printf("nar %d %u %d %u\n", (int)c, (unsigned)uc, (int)s,
	       (unsigned)us);
	return c + uc + s + us;
}

double tdbl(double a, double b)
{
	double x = a;
	double acc = 0.0;
	int i;

	for (i = 0; i < 6; i += 1) {
		x += b;
		x -= 0.75;
		x *= 1.125;
		x /= 2.5;		/* diveqd: the helper path */
		acc += x;
		x++;			/* postincd: the helper path */
		++x;
		x--;
		--x;
	}
	printf("dbl %.6f %.6f\n", x, acc);
	return x + acc;
}

float tflt(float a, float b)
{
	float x = a;
	float acc = 0.0f;
	int i;

	for (i = 0; i < 6; i += 1) {
		x += b; x -= 0.5f; x *= 1.25f; x /= 2.0f;
		acc += x;
		x++; --x;
	}
	printf("flt %.6f %.6f\n", (double)x, (double)acc);
	return x + acc;
}

int main()
{
	int r = 0;

	r += tint(1234, 987);
	r += (int)tuns(99999u, 12345u);
	r += (int)tll(1234567890123LL, 987654321LL);
	r += (int)tull(18446744073709551000ULL, 5555555ULL);
	r += tnarrow(45);
	r += (int)tdbl(3.25, 1.625);
	r += (int)tflt(2.5f, 0.375f);
	printf("sum %d\n", r);
	return 0;
}

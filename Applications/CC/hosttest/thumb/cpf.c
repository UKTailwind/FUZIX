/* CP-F: 64-bit values - moves, the inline ALU pairs, the helper-routed
   mul/div/rem/shifts, compares both signednesses, and the widening and
   truncating conversions across the native call seam. */
#include <stdio.h>

long long add64(long long a, long long b) { return a + b; }
long long sub64(long long a, long long b) { return a - b; }
long long mul64(long long a, long long b) { return a * b; }
long long div64(long long a, long long b) { return a / b; }
long long rem64(long long a, long long b) { return a % b; }
unsigned long long divu64(unsigned long long a, unsigned long long b) { return a / b; }
unsigned long long remu64(unsigned long long a, unsigned long long b) { return a % b; }
long long and64(long long a, long long b) { return a & b; }
long long or64(long long a, long long b) { return a | b; }
long long xor64(long long a, long long b) { return a ^ b; }
long long shl64(long long a, int n) { return a << n; }
long long shr64(long long a, int n) { return a >> n; }
unsigned long long shru64(unsigned long long a, int n) { return a >> n; }
long long neg64(long long a) { return -a; }
long long not64(long long a) { return ~a; }
int bool64(long long a) { return !!a; }
int lnot64(long long a) { return !a; }

int lt64(long long a, long long b) { return a < b; }
int le64(long long a, long long b) { return a <= b; }
int gt64(long long a, long long b) { return a > b; }
int ge64(long long a, long long b) { return a >= b; }
int eq64(long long a, long long b) { return a == b; }
int ne64(long long a, long long b) { return a != b; }
int ltu64(unsigned long long a, unsigned long long b) { return a < b; }
int geu64(unsigned long long a, unsigned long long b) { return a >= b; }

long long widen(int a) { return a; }
unsigned long long widenu(unsigned a) { return a; }
int narrow(long long a) { return (int)a; }

long long sumto(long long n)
{
	long long s = 0;
	long long i;
	for (i = 1; i <= n; i++)
		s += i;
	return s;
}

/* 64-bit xorshift, checksummed: touches shifts, xor, load/store64 */
unsigned long long rng64(int n)
{
	unsigned long long x = 88172645463325252ull;
	int i;
	for (i = 0; i < n; i++) {
		x ^= x << 13;
		x ^= x >> 7;
		x ^= x << 17;
	}
	return x;
}

/* mixed-width call chain: 64-bit results feeding 32-bit args and back */
int low32(long long v) { return (int)(v & 0x7FFFFFFF); }
long long chain(int a)
{
	return sumto(low32(mul64(a, a + 1)) & 1023) + widen(a);
}

int main(void)
{
	long long big = 0x123456789ALL;
	long long nbig = -0x87654321FLL;

	printf("%lld %lld %lld\n", add64(big, nbig), sub64(big, nbig),
	       mul64(123456789LL, 987654321LL));
	printf("%lld %lld %lld %lld\n", div64(big, 100000LL),
	       rem64(big, 100000LL), div64(nbig, 12345LL),
	       rem64(nbig, 12345LL));
	printf("%llu %llu\n", divu64(0xFFFFFFFFFFFFFFFFull, 3),
	       remu64(0xFEDCBA9876543210ull, 0x123456789ull));
	printf("%llx %llx %llx\n",
	       (unsigned long long)and64(big, 0xFF00FF00FF00LL),
	       (unsigned long long)or64(big, 0xF0F0F0F0F0F0LL),
	       (unsigned long long)xor64(big, nbig));
	printf("%llx %llx %llx %llx\n",
	       (unsigned long long)shl64(big, 20),
	       (unsigned long long)shr64(nbig, 7),
	       shru64(0xFEDCBA9876543210ull, 36),
	       (unsigned long long)shl64(1LL, 62));
	printf("%lld %llx %d %d %d %d\n", neg64(big),
	       (unsigned long long)not64(big),
	       bool64(big), bool64(0), lnot64(nbig), lnot64(0));
	printf("%d%d%d%d%d%d", lt64(nbig, big), le64(big, big),
	       gt64(big, nbig), ge64(nbig, big), eq64(big, big),
	       ne64(big, nbig));
	printf(" %d%d%d%d%d%d\n", lt64(big, nbig), le64(big, nbig),
	       gt64(nbig, big), ge64(big, nbig), eq64(big, nbig),
	       ne64(big, big));
	/* high-word-only differences: the compare must see both halves */
	printf("%d%d%d%d\n",
	       lt64(0x100000000LL, 0x200000000LL),
	       eq64(0x100000005LL, 0x200000005LL),
	       ltu64(0x8000000000000000ull, 1),
	       geu64(0x8000000000000000ull, 1));
	printf("%lld %llu %d %d\n", widen(-42), widenu(0xFFFFFFFFu),
	       narrow(0x1FFFFFFFFLL), narrow(-1LL));
	printf("%lld %llx\n", sumto(100000LL), rng64(10000));
	printf("%lld\n", chain(999));
	return 0;
}

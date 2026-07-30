#include <stdio.h>

int lt(int a, int b) { return a < b; }
int le(int a, int b) { return a <= b; }
int gt(int a, int b) { return a > b; }
int ge(int a, int b) { return a >= b; }
int eq(int a, int b) { return a == b; }
int ne(int a, int b) { return a != b; }
unsigned ltu(unsigned a, unsigned b) { return a < b; }
unsigned geu(unsigned a, unsigned b) { return a >= b; }
int lnot(int a) { return !a; }
int both(int a, int b) { return a && b; }
int gcd(int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; }
int sumsq(int n) { int s = 0; int i; for (i = 1; i <= n; i++) s += i * i; return s; }
int collatz(int n) { int c = 0; while (n != 1) { if (n & 1) n = 3 * n + 1; else n /= 2; c++; } return c; }
int nprimes(int lim)
{
	static char f[10000];
	int i, k, c = 0;
	for (i = 0; i <= lim; i++) f[i] = 1;
	for (i = 2; i <= lim; i++)
		if (f[i]) {
			for (k = i + i; k <= lim; k += i) f[k] = 0;
			c++;
		}
	return c;
}

int main(void)
{
	printf("%d%d%d%d%d%d\n",
	       lt(1, 2), le(2, 2), gt(3, 2), ge(2, 3), eq(5, 5), ne(5, 5));
	printf("%d%d%d%d%d%d\n",
	       lt(2, 1), le(3, 2), gt(2, 3), ge(3, 2), eq(4, 5), ne(4, 5));
	printf("%u %u %u %u\n", ltu(1, 0x80000000u), geu(0x80000000u, 1),
	       ltu(5, 3), geu(3, 5));
	printf("%d %d %d %d\n", lnot(0), lnot(7), both(2, 3), both(2, 0));
	printf("%d %d\n", gcd(1071, 462), gcd(17, 5));
	printf("%d %d\n", sumsq(100), collatz(27));
	printf("%d\n", nprimes(8191));
	return 0;
}

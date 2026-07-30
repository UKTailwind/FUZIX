#include <stdio.h>

int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }
int mul(int a, int b) { return a * b; }
int divs(int a, int b) { return a / b; }
unsigned divu(unsigned a, unsigned b) { return a / b; }
int rems(int a, int b) { return a % b; }
unsigned remu(unsigned a, unsigned b) { return a % b; }
int band(int a, int b) { return a & b; }
int bor(int a, int b) { return a | b; }
int bxor(int a, int b) { return a ^ b; }
int shl(int a, int b) { return a << b; }
int shrs(int a, int b) { return a >> b; }
unsigned shru(unsigned a, unsigned b) { return a >> b; }
int neg(int a) { return -a; }
int bnot(int a) { return ~a; }
int idx(int *p, int i) { return p[i]; }
int sumsq(int n) { int s; int i; s = 0; for (i = 1; i <= n; i = i + 1) s += i * i; return s; }
int mix(int a, int b, int c) { return (a + b) * c - a / (b | 1); }
char csum(char *p, int n) { char s; int i; s = 0; for (i = 0; i < n; i++) s += p[i]; return s; }
unsigned short usinc(unsigned short *p) { *p += 3; return (*p)++; }

int arr[8];

int main(void)
{
	int i;
	char buf[5];

	printf("%d %d %d %d\n", add(17, 25), sub(9, 30), mul(-7, 12), divs(-100, 7));
	printf("%u %d %u\n", divu(3000000000u, 7), rems(-100, 7), remu(3000000000u, 7));
	printf("%d %d %d\n", band(0xF0F0, 0x1234), bor(0xF000, 0x00F0), bxor(0xFFFF, 0x1234));
	printf("%d %d %u\n", shl(3, 10), shrs(-4096, 4), shru(0x80000000u, 4));
	printf("%d %d\n", neg(-42), bnot(0));
	for (i = 0; i < 8; i++)
		arr[i] = i * 100;
	printf("%d %d\n", idx(arr, 3), idx(arr, 7));
	printf("%d %d\n", sumsq(10), mix(5, 9, 3));
	for (i = 0; i < 5; i++)
		buf[i] = (char)(i * 3 + 60);
	printf("%d\n", csum(buf, 5));
	{
		unsigned short w = 100;
		printf("%d %d\n", usinc(&w), w);
	}
	return 0;
}

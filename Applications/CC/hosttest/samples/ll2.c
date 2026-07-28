/* Values that do not fit in 32 bits. */
int printf();

long long big;

int main(void)
{
	long long a;

	a = 1;
	a = a << 40;                    /* 2^40, needs the high half */
	printf("shift hi=%d lo=%d\n", (int)(a >> 32), (int)a);

	big = 5000000000LL;             /* > 2^32 */
	printf("big   hi=%d lo=%d\n", (int)(big >> 32), (int)big);

	a = 2000000000;
	a = a + a;                      /* 4e9, overflows 32-bit signed */
	printf("sum   hi=%d lo=%d\n", (int)(a >> 32), (int)a);
	return 0;
}

/* long long: does it parse, size, load, store and do arithmetic? */
int printf();

long long gll = 1;
unsigned long long gull = 2;

int main(void)
{
	long long a;
	unsigned long long b;

	printf("sizes %d %d %d %d\n",
	       (int)sizeof(char), (int)sizeof(short),
	       (int)sizeof(long), (int)sizeof(long long));
	a = 5;
	b = 7;
	printf("vals  %d %d\n", (int)a, (int)b);
	a = a + 1;
	printf("add   %d\n", (int)a);
	printf("glob  %d %d\n", (int)gll, (int)gull);
	return 0;
}

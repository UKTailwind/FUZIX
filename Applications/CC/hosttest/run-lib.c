int printf();
int putchar();

char *msg = "bytecode";

int fib(int n)
{
	if (n < 2)
		return n;
	return fib(n - 1) + fib(n - 2);
}

int main(void)
{
	int i;

	printf("hello from %s\n", msg);
	for (i = 0; i < 10; i++)
		printf("%d ", fib(i));
	putchar('\n');
	printf("%d %d %x\n", -7, 1000000, 255);
	return 0;
}

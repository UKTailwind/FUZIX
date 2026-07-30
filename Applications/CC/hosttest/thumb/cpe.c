#include <stdio.h>

int fib(int n)
{
	if (n < 2)
		return n;
	return fib(n - 1) + fib(n - 2);
}

int odd(int n);
int even(int n) { return n == 0 ? 1 : odd(n - 1); }
int odd(int n) { return n == 0 ? 0 : even(n - 1); }

int acc;
int addto(int v) { acc += v; return acc; }

int apply(int (*f)(int), int v) { return f(v); }

int chatter(int n)
{
	int i, s = 0;
	for (i = 0; i < n; i++) {
		s += fib(i);
		printf("fib(%d)=%d ", i, fib(i));
	}
	printf("\n");
	return s;
}

int sum6(int a, int b, int c, int d, int e, int f)
{
	return a + b + c + d + e + f;
}

int main(void)
{
	printf("%d %d\n", fib(15), fib(20));
	printf("%d %d %d %d\n", even(10), odd(10), even(7), odd(7));
	addto(5); addto(7);
	printf("%d\n", addto(30));
	printf("%d %d\n", apply(fib, 10), apply(addto, 1));
	printf("%d\n", chatter(8));
	printf("%d\n", sum6(1, 2, 3, 4, 5, 6));
	return 0;
}

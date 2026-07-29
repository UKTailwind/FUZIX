/*
 *	Unary plus. ANSI added it, so it is C89, and it was not implemented
 *	at all - "+5" and "60 + +3" were both rejected outright.
 *
 *	c-testsuite 00202. The operator does nothing to the value, so what
 *	is really being checked is that it parses everywhere it may appear
 *	and binds as a unary operator should.
 */
#include <stdio.h>

int f(int x)
{
	return +x;
}

int main(void)
{
	int a = 5;
	int arr[3];
	double d = +2.5;
	unsigned u = +7u;
	long l = +100000L;

	arr[0] = 1;
	arr[1] = 2;
	arr[2] = 3;

	/* The forms that used to be refused */
	printf("%d %d %d\n", +5, 60 + +3, + +7);

	/* Stacked with the other unary operators, in both orders */
	printf("%d %d %d %d\n", -+-a, +-+a, +~a, !+a);

	/* Binds tighter than the binary operators around it */
	printf("%d %d %d\n", 2 * +3, +2 * 3, 10 - +4);

	/* On an lvalue, a call, a subscript and a cast */
	printf("%d %d %d %d\n", +a, +f(9), +arr[1], +(int)d);

	/* Other arithmetic types */
	printf("%.1f %u %ld\n", +d, u, l);

	/* A constant expression context */
	switch (a) {
	case +5:
		printf("case +5\n");
		break;
	default:
		printf("wrong case\n");
		break;
	}

	return 0;
}

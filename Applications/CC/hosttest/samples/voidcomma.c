/*
 * Casting to void, and the comma operator.
 *
 * c-testsuite 00212 for the first. The second was found by writing the
 * test for the first: nothing had ever generated T_COMMA, so it fell
 * through to the backend's "unknown operator" path and came out as a
 * call to "__op2c" that does not exist. Both are plain C89 and both
 * are everywhere in real source, "for (i = 0, j = n; i < j; i++, j--)"
 * most of all.
 *
 * The comma cases matter more than they look: the left operand must be
 * evaluated for its side effects and its value thrown away, and the
 * expression's value and type are the right operand's.
 */

int printf();

int side = 0;

int bump(int n)
{
	side += n;
	return side;
}

void nothing(void)
{
}

int main(void)
{
	int x = 5;
	char c = 'q';
	long l = 7;
	int *p = &x;
	int i, j, t;

	/* ---- cast to void ---- */

	(void) printf("a ok\n");

	/* the operand is still evaluated for its side effects */
	(void) bump(3);
	(void) bump(4);
	printf("b %d\n", side);

	/* assorted types, including a call that is already void */
	(void) x;
	(void) c;
	(void) l;
	(void) p;
	(void) *p;
	(void) (x + 1);
	(void) nothing();
	printf("c %d %d\n", x, side);

	/* an assignment cast to void */
	(void) (x = 11);
	printf("d %d\n", x);

	/* ---- the comma operator ---- */

	/* value and type come from the right operand */
	t = (1, 2, 3);
	printf("e %d\n", t);

	/* the left operand's side effects still happen */
	side = 0;
	t = (bump(5), bump(6));
	printf("f %d %d\n", t, side);

	/* a pure left operand can be discarded entirely */
	t = (x, 42);
	printf("g %d\n", t);

	/* the classic two-ended loop */
	{
		int a[6];
		for (i = 0; i < 6; i++)
			a[i] = i;
		for (i = 0, j = 5; i < j; i++, j--) {
			t = a[i];
			a[i] = a[j];
			a[j] = t;
		}
		printf("h");
		for (i = 0; i < 6; i++)
			printf(" %d", a[i]);
		printf("\n");
	}

	/* comma in a condition, and nested commas */
	side = 0;
	i = 0;
	while (bump(1), i < 3)
		i++;
	printf("i %d %d\n", i, side);

	/* comma of differing types - the result is the right one */
	{
		long r;
		r = (c, l);
		printf("j %d\n", (int) r);
	}

	/* comma inside a function argument must not split the argument,
	   because it is bracketed */
	printf("k %d\n", (side = 0, bump(9)));

	return 0;
}

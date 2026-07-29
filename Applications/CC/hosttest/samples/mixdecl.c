/*
 * Declarations mixed in among statements.
 *
 * C89 wants every declaration at the head of a block; this is the C99
 * rule, and it is what everyone actually writes. Refusing it is a
 * nuisance out of all proportion to the standard it comes from, so the
 * compiler is C89 plus this.
 *
 * A pure relaxation - nothing legal before changes meaning - but the
 * things worth checking are that scoping, shadowing and initialisation
 * behave the same wherever the declaration sits, and that a declaration
 * is still refused where only a statement is allowed.
 *
 * Nine of the c-testsuite failures were this and nothing else.
 */

int printf();

int side;

int bump(int n)
{
	side += n;
	return n;
}

int main(void)
{
	int a = 1;

	printf("a %d\n", a);

	/* the basic case: a declaration after a statement */
	int b = 2;
	printf("b %d\n", b);

	/* one initialised from the value of an earlier statement */
	a = a + 10;
	int c = a * 2;
	printf("c %d %d\n", a, c);

	/* several in a row, part way down */
	printf("mid\n");
	int d = 4, e = 5;
	char f = 'f';
	printf("d %d %d %c\n", d, e, f);

	/* an aggregate, which has to be initialised in place */
	int arr[3] = { 7, 8, 9 };
	printf("arr %d %d %d\n", arr[0], arr[1], arr[2]);

	/* a declaration whose initialiser has side effects, so the order
	   of evaluation against the surrounding statements is visible */
	side = 0;
	bump(1);
	int g = bump(2);
	bump(3);
	printf("g %d %d\n", g, side);

	/* shadowing still works, and the shadow still ends with the block */
	{
		printf("outer b %d\n", b);
		int b = 99;
		printf("inner b %d\n", b);
	}
	printf("outer b again %d\n", b);

	/* inside a loop body, so the declaration is reached repeatedly and
	   re-initialised each time */
	{
		int i;
		for (i = 0; i < 3; i++) {
			printf("iter %d\n", i);
			int fresh = i * 10;
			fresh += 1;
			printf("fresh %d\n", fresh);
		}
	}

	/* after a label, and inside a switch arm */
	{
		int n = 1;
		switch (n) {
		case 1:
			printf("case\n");
			int inswitch = 42;
			printf("inswitch %d\n", inswitch);
			break;
		default:
			break;
		}
	}

	/* a declaration as the last thing in a block */
	{
		printf("last\n");
		int trailing = 7;
		printf("trailing %d\n", trailing);
	}

	return 0;
}

/*
 *	Unbounded recursion in a function small enough for the translator
 *	to take natively.  Before the stack guard went into the native
 *	prologue this ran off the bottom of the VM stack and, on the
 *	board, took the machine down with the video attached to it.  It
 *	must now stop with "stack overflow - recursion too deep?".
 *
 *	Deliberately not in the qemutests set, which checks that programs
 *	produce the right answer - this one is expected to die.
 */
int printf();

static int depth;

static int down(int a, int b, int c, int d, int e, int f)
{
	depth++;
	/* Six arguments and a local, so each level really costs stack,
	   and all of them used so nothing is optimised away. */
	return down(a + 1, b, c, d, e, f) + a + b + c + d + e + f;
}

int main(void)
{
	printf("recursing...\n");
	down(1, 2, 3, 4, 5, 6);
	printf("returned - the guard did not fire, depth %d\n", depth);
	return 0;
}

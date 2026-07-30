/*
 *	Stage 3 vehicle: add2 compiles to bytecode as a stub whose
 *	+1000000 gives it away; natpatch.py then overwrites it with the
 *	hand-written Thumb of add2.s.  Correct sums WITHOUT the million
 *	prove the native path ran - through the same call_target
 *	dispatch, frame layout and register file the emitter will use.
 */
#include <stdio.h>

int add2(int a, int b)
{
	return a + b + 1000000;	/* stub body - see above */
}

int main(void)
{
	int i, s = 0;

	printf("add2(2,3) = %d\n", add2(2, 3));
	printf("add2(-5,7) = %d\n", add2(-5, 7));
	for (i = 0; i < 10; i++)
		s += add2(i, i * i);
	printf("sum = %d\n", s);
	printf("nested = %d\n", add2(add2(1, 2), add2(3, 4)));
	return 0;
}

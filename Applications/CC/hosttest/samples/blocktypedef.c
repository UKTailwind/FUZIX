/*
 *	typedef inside a block. c-testsuite 00198.
 *
 *	Typedefs were recognised only in toplevel(), so *any* typedef in a
 *	block was rejected - not just the anonymous enum the test looked
 *	like it was about.
 *
 *	The interesting half is scope. A block typedef must go out of
 *	scope with its block and must shadow an outer one while it is
 *	live, which the symbol table could not express: S_TYPEDEF sorts
 *	above S_STATIC, so pop_local_symbols() treated it as permanent,
 *	and find_symbol_by_class() returned the *outermost* match because
 *	it walks backwards recording every non-local hit. Both are why
 *	sizeof is checked below rather than just the values - a wrong
 *	lookup still assigns and prints correctly when the types happen to
 *	agree on the value.
 */
#include <stdio.h>

typedef int outer;
typedef int samename;

outer g = 100;

static int useit(samename v)
{
	return v * 2;
}

int main(void)
{
	typedef int myint;
	typedef char *str;
	typedef struct { int x, y; } pt;
	typedef unsigned char small;

	myint a = 5;
	str s = "block typedef";
	pt p;
	small sm = 200;

	p.x = 3;
	p.y = 4;
	printf("%d %s %d %d %d\n", a, s, p.x, p.y, g);
	printf("%u %d\n", sm, (int)sizeof(small));
	printf("%d\n", useit(21));

	{
		/* Shadow both the file scope typedef and the one above */
		typedef char outer;
		typedef char myint;
		outer c = 'A';
		myint d = 'B';

		printf("%c %c %d %d\n", c, d,
			(int)sizeof(outer), (int)sizeof(myint));
	}

	/* Out of the inner block both must be int again */
	printf("%d %d\n", (int)sizeof(outer), (int)sizeof(myint));
	{
		outer big = 70000;
		myint m = -70000;

		printf("%d %d\n", big, m);
	}

	/* A typedef declared after a statement, and one naming a type
	   that only exists inside this block */
	printf("mid\n");
	{
		typedef pt point;
		point q;

		q.x = 7;
		q.y = 8;
		printf("%d %d %d\n", q.x, q.y, (int)sizeof(point));
	}
	return 0;
}

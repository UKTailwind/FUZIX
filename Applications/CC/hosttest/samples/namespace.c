/*
 *	The declaration and namespace gaps closed together with the
 *	conformance run: c-testsuite 00078, 00114, 00124, 00129 and 00144.
 *
 *	C keeps ordinary identifiers, struct/union tags, struct members and
 *	labels in separate namespaces, and an inner declaration of any of
 *	them shadows an outer one. Each of these was rejected outright.
 */
#include <stdio.h>

/* 00114: a prototype and a K&R definition of the same function are
   compatible - f(void) and f() differ only in whether the arguments
   were stated. */
int kr(void);

int kr()
{
	return 7;
}

/* 00124: a function returning a pointer to a function, with parameter
   names repeated between the two lists. They are separate scopes, so
   the repeated "b" is legal. */
static int sub(int c, int b)
{
	return c - b;
}

static int (*pick(int a, int b))(int c, int b)
{
	if (a != b)
		return sub;
	return 0;
}

/* 00129: a typedef, a tag and a variable may all be called the same
   thing, and members may share the name too. */
typedef struct s s;

struct s {
	struct s1 {
		int s;
	} s;
	int x;
};

typedef int myt;

static int useparam(myt v)
{
	return v + 1;
}

int main(void)
{
	/* 00078: a function declared inside a block. It has no storage,
	   so it must not reach assign_storage - that gave "can't size
	   type". */
	int inner(char *);
	struct s s;
	int *q;
	void *p;
	int i;

	/* An ordinary identifier shadowing a typedef name */
	int myt;

	myt = 5;
	s.s.s = 3;
	s.x = 4;
	q = &s.x;
	p = q;
	i = 0;

	printf("%d %d %d %d\n", kr(), s.s.s, s.x, myt);
	printf("%d\n", (*pick(0, 2))(9, 4));
	printf("%d %d\n", inner("ab"), useparam(41));

	/* 00144: a null pointer constant in ?: - the result takes the
	   pointer type from whichever side is not the constant. */
	p = i ? (void *) 0 : 0;
	p = i ? 0 : (void *) 0;
	q = i ? 0 : q;
	q = i ? q : 0;
	printf("%d %d\n", p == 0, q == 0);

	/* Labels have their own namespace, so a label may share a name
	   with a variable in scope. */
	goto s;
	printf("not reached\n");
s:
	{
		int s;		/* and an inner block may reuse it again */
		s = 42;
		printf("%d\n", s);
	}
	return 0;
}

int inner(char *t)
{
	return (int)t[0];
}

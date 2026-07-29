/*
 * Struct and union tags are scoped like any other name.
 *
 * c-testsuite 00044 and 00053. A tag redeclared in an inner block is a
 * new and distinct type, not a redefinition of the outer one, and it
 * stops existing when its block does.
 *
 * Two things were wrong. find_struct searched the symbol table forwards
 * and returned the first match, so the *outermost* tag always won -
 * ordinary identifiers have always searched backwards for exactly this
 * reason. And tags were never discarded at the end of a block, because
 * pop_local_symbols keeps anything whose storage class is at or above
 * S_STATIC and S_STRUCT is well above it.
 *
 * Position in the table cannot decide which tags to discard either: a
 * file scope tag may sit above the mark a block was entered at. So a
 * tag records whether it was declared inside a function body.
 */

int printf();

/* file scope, and forward declared first */
struct T;

struct T {
	int x;
};

union U {
	int i;
	char c;
};

struct T filescope;

int outer(void)
{
	struct T v;
	v.x = 2;
	/* an inner block defines its own T, which must not disturb v */
	{
		struct T { int z; };
	}
	return v.x;
}

int distinct(void)
{
	struct T { int x; } s1;
	s1.x = 1;
	{
		struct T { int y; } s2;
		s2.y = 40;
		/* different types, both called T */
		return s1.x + s2.y;
	}
}

/* after an inner block has ended, T is the file scope one again */
int restored(void)
{
	{
		struct T { int z; } inner;
		inner.z = 9;
	}
	{
		struct T v;	/* the file scope T, with member x */
		v.x = 3;
		return v.x;
	}
}

/* unions scope the same way */
int unions(void)
{
	union U a;
	a.i = 7;
	{
		union U { long l; } b;
		b.l = 100;
		return a.i + (int) b.l;
	}
}

/* a tag first seen inside a function is local to it: this one is a
   different T again, and does not leak out to the next function */
int localonly(void)
{
	struct T { char ch; } t;
	t.ch = 'A';
	return t.ch;
}

int afterlocal(void)
{
	struct T v;		/* file scope T once more */
	v.x = 11;
	return v.x;
}

int main(void)
{
	filescope.x = 5;
	printf("file %d\n", filescope.x);
	printf("outer %d\n", outer());
	printf("distinct %d\n", distinct());
	printf("restored %d\n", restored());
	printf("unions %d\n", unions());
	printf("localonly %d\n", localonly());
	printf("afterlocal %d\n", afterlocal());
	return 0;
}

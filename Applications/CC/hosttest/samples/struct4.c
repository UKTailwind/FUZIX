/*
 * Returning structs and unions by value.
 *
 * The ABI is a hidden first argument holding the address of space the
 * caller reserved; the function copies its result there and returns
 * that address. So the interesting cases are the ones where the
 * caller's temporary has to survive: a call used directly as a member
 * reference, as an argument to another call, and two calls live in the
 * same expression.
 */

int printf();

struct point {
	int x;
	int y;
};

struct odd {
	char a;
	char b;
	char c;
};

struct big {
	struct point tl;
	struct point br;
	long long stamp;
};

union u {
	int i;
	char c[4];
};

struct point g;

struct point mk(int x, int y)
{
	struct point p;
	p.x = x;
	p.y = y;
	return p;
}

/* no arguments at all: the hidden pointer is the only one */
struct point origin(void)
{
	struct point p;
	p.x = 0;
	p.y = 0;
	return p;
}

/* returns one of its arguments, so the copy is argument to hidden */
struct point pick(struct point a, struct point b, int which)
{
	if (which)
		return a;
	return b;
}

/* returns a global */
struct point getg(void)
{
	return g;
}

/* returns the result of another struct returning call */
struct point twice(int x)
{
	return mk(x, x * 2);
}

/* a size that is not a multiple of four */
struct odd mkodd(int base)
{
	struct odd o;
	o.a = base;
	o.b = base + 1;
	o.c = base + 2;
	return o;
}

/* larger, nested, and containing a 64-bit member */
struct big mkbig(int n)
{
	struct big b;
	b.tl = mk(n, n + 1);
	b.br = mk(n + 2, n + 3);
	b.stamp = 1234567890123LL;
	return b;
}

union u mku(int i)
{
	union u v;
	v.i = i;
	return v;
}

int sum(struct point p)
{
	return p.x + p.y;
}

/* recursion, so the hidden pointer has to be per invocation */
struct point walk(int n)
{
	struct point p;
	if (n == 0)
		return origin();
	p = walk(n - 1);
	p.x += n;
	p.y += n * n;
	return p;
}

/* called through a pointer, which is a different call opcode */
struct point apply(struct point (*fp)(), int a, int b)
{
	return fp(a, b);
}

int main(void)
{
	struct point a, b;
	struct odd o;
	struct big bg;
	union u v;
	int i;

	a = mk(3, 4);
	printf("mk %d %d\n", a.x, a.y);

	b = origin();
	printf("origin %d %d\n", b.x, b.y);

	/* member of a call's result, with no assignment in between */
	printf("member %d %d\n", mk(7, 8).x, mk(7, 8).y);

	/* the result fed straight into a call that takes a struct */
	printf("feed %d\n", sum(mk(5, 6)));

	/* two live calls in one expression */
	printf("two %d\n", sum(mk(1, 2)) + sum(mk(10, 20)));

	/* nested: a struct returning call as the argument of another */
	printf("nested %d\n", sum(pick(mk(1, 2), mk(3, 4), 1)));
	printf("nested %d\n", sum(pick(mk(1, 2), mk(3, 4), 0)));

	/* a call whose return is another call's return */
	a = twice(6);
	printf("twice %d %d\n", a.x, a.y);

	g.x = 11;
	g.y = 22;
	a = getg();
	g.x = 0;
	printf("getg %d %d\n", a.x, a.y);

	o = mkodd(1);
	printf("odd %d %d %d\n", (int) o.a, (int) o.b, (int) o.c);

	bg = mkbig(100);
	printf("big %d %d %d %d %d\n", bg.tl.x, bg.tl.y, bg.br.x, bg.br.y,
	       (int) (bg.stamp % 1000));

	v = mku(0x41424344);
	printf("uni %d %d\n", (int) v.c[0], (int) v.c[3]);

	/* in a loop, so a leak in the caller's temporaries accumulates */
	for (i = 0; i < 4; i++) {
		a = mk(i, i * i);
		printf("loop %d %d %d\n", i, a.x, a.y);
	}

	a = walk(4);
	printf("walk %d %d\n", a.x, a.y);

	a = apply(mk, 8, 9);
	printf("apply %d %d\n", a.x, a.y);

	/* the result discarded entirely */
	mk(99, 99);
	printf("done\n");

	return 0;
}

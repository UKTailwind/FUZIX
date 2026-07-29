/*
 * Passing structs and unions by value.
 *
 * The point of most of this is the argument stack accounting rather
 * than the copy itself: a struct argument occupies a whole number of
 * words, and the caller has to take back exactly what it pushed. So
 * the cases here vary the position of the struct in the argument list,
 * mix it with scalars of every width, and use odd sizes that do not
 * divide by four.
 *
 * Returning a struct is a separate step and is not exercised here.
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
	char tag;
};

union u {
	int i;
	char c[4];
};

struct point g = { 100, 200 };

/* the only argument */
int sum(struct point p)
{
	return p.x + p.y;
}

/* modifying the copy must not touch the caller's object */
int bump(struct point p)
{
	p.x += 1000;
	p.y += 1000;
	return p.x + p.y;
}

/* struct first, scalars after */
int first(struct point p, int a, int b)
{
	return p.x * 1000 + p.y * 100 + a * 10 + b;
}

/* struct last */
int last(int a, int b, struct point p)
{
	return p.x * 1000 + p.y * 100 + a * 10 + b;
}

/* struct in the middle, with a char and a long long either side */
int middle(char c, struct point p, long long l)
{
	return (int) c + p.x + p.y + (int) l;
}

/* a size that is not a multiple of four */
int odd3(struct odd o, int t)
{
	return (int) o.a + (int) o.b + (int) o.c + t;
}

/* two of them in a row, so the second's offset depends on the first
   having been rounded up */
int odd6(struct odd o, struct odd p, int t)
{
	return (int) o.a * 100 + (int) p.a * 10 + t;
}

/* nested, and larger than any scalar */
int big(struct big b, int t)
{
	return b.tl.x + b.tl.y + b.br.x + b.br.y + (int) b.tag + t;
}

int uni(union u v)
{
	return v.i;
}

/* two structs and nothing else */
int pair(struct point a, struct point b)
{
	return a.x * 1000 + a.y * 100 + b.x * 10 + b.y;
}

int main(void)
{
	struct point a;
	struct odd o, p;
	struct big bg;
	union u v;
	int i;

	a.x = 3;
	a.y = 4;

	printf("sum %d\n", sum(a));
	printf("bump %d\n", bump(a));
	printf("intact %d %d\n", a.x, a.y);

	printf("first %d\n", first(a, 5, 6));
	printf("last %d\n", last(5, 6, a));
	printf("middle %d\n", middle('A', a, 100LL));

	o.a = 1;
	o.b = 2;
	o.c = 3;
	p.a = 9;
	p.b = 8;
	p.c = 7;
	printf("odd3 %d\n", odd3(o, 10));
	printf("odd6 %d\n", odd6(o, p, 5));

	bg.tl.x = 1;
	bg.tl.y = 2;
	bg.br.x = 10;
	bg.br.y = 20;
	bg.tag = 3;
	printf("big %d\n", big(bg, 4));

	v.i = 0;
	v.c[0] = 1;
	v.c[1] = 2;
	printf("uni %d\n", uni(v));

	printf("pair %d\n", pair(a, g));

	/* a global as the argument, and a member of a global struct */
	printf("global %d\n", sum(g));

	/* in a loop, so a leak in the stack accounting accumulates
	   instead of cancelling out */
	for (i = 0; i < 4; i++) {
		a.x = i;
		a.y = i * i;
		printf("loop %d %d\n", i, first(a, i, 1));
	}

	/* nested calls: an argument list built while another is part
	   built */
	printf("nest %d\n", first(a, sum(g), last(1, 2, a)));

	return 0;
}

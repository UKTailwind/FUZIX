/*
 * Whole-struct assignment: local to local, local to global, global to
 * local, nested structs and arrays of them. Passing and returning by
 * value are separate steps and are not exercised here.
 */

int printf();

struct point {
	int x;
	int y;
};

struct box {
	struct point tl;
	struct point br;
	char tag;
};

struct point g;
struct box gb;
struct point arr[3];

int main(void)
{
	struct point a, b;
	struct box c;
	int i;

	a.x = 3;
	a.y = 4;

	b = a;			/* local to local */
	g = a;			/* local to global */
	a.x = 99;		/* must not disturb either copy */
	printf("copy %d %d %d %d\n", b.x, b.y, g.x, g.y);

	a = g;			/* global to local */
	printf("back %d %d\n", a.x, a.y);

	/* a struct containing structs */
	c.tl.x = 1;
	c.tl.y = 2;
	c.br.x = 10;
	c.br.y = 20;
	c.tag = 'z';
	gb = c;
	c.tl.x = 0;
	printf("nest %d %d %d %d %d\n", gb.tl.x, gb.tl.y, gb.br.x, gb.br.y,
	       (int) gb.tag);

	/* the inner struct on its own */
	b = c.br;
	printf("inner %d %d\n", b.x, b.y);

	/* through an array */
	for (i = 0; i < 3; i++) {
		a.x = i;
		a.y = i * i;
		arr[i] = a;
	}
	printf("arr %d %d %d %d\n", arr[0].x, arr[1].x, arr[2].x, arr[2].y);

	/* chained, which is why the copy leaves the destination behind */
	a.x = 7;
	a.y = 8;
	g = b = a;
	printf("chain %d %d %d %d\n", b.x, b.y, g.x, g.y);

	return 0;
}

/*
 *	Elided braces in aggregate initialisers - C89 6.5.7, c-testsuite
 *	00205.
 *
 *	An inner initialiser need not be braced; when it is not, it takes
 *	as many items as it needs from the list its parent is reading.
 *
 *	This one is worth guarding carefully. The bug it fixes was not
 *	"expected {" alone - the other half is the comma. An elided group
 *	has to stop when its quota is full and leave the separator for the
 *	parent, because eating it makes the parent think its own list ended
 *	early. That failure mode does not produce a diagnostic, it produces
 *	an object with the right shape and the wrong contents, so every
 *	field is checked below and not just the first of each group.
 */
#include <stdio.h>

struct pt {
	int c[4];
	int b, e, k;
};

/* Fully elided: one flat list for an array of structs each containing
   an array. */
struct pt flat[] = {
	1, 2, 3, 4, 10, 20, 30,
	5, 6, 7, 8, 40, 50, 60,
};

/* Fully braced - must still work exactly as it did before. */
struct pt braced[] = {
	{ { 11, 12, 13, 14 }, 15, 16, 17 },
	{ { 21, 22, 23, 24 }, 25, 26, 27 },
};

/* Mixed: outer braces, inner elided. */
struct pt mixed[] = {
	{ 31, 32, 33, 34, 35, 36, 37 },
	{ 41, 42, 43, 44, 45, 46, 47 },
};

/* Short groups: the rest of each object is zero filled. */
struct pt part[2] = {
	{ { 1 }, 2 },
	{ { 3, 4 } },
};

/* Multidimensional arrays, elided and braced. */
int grid[2][3] = { 1, 2, 3, 4, 5, 6 };
int grid2[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
int part2[3][2] = { { 1 }, { 2, 3 } };

/* A nested struct, elided right through. */
struct outer {
	struct pt p;
	int tail;
};
struct outer deep[] = { 1, 2, 3, 4, 5, 6, 7, 99 };

static void show(const char *tag, struct pt *p, int n)
{
	int i, j;
	for (i = 0; i < n; i++) {
		printf("%s[%d]", tag, i);
		for (j = 0; j < 4; j++)
			printf(" c%d=%d", j, p[i].c[j]);
		printf(" b=%d e=%d k=%d\n", p[i].b, p[i].e, p[i].k);
	}
}

int main(void)
{
	int i, j;
	/* The same shapes again as automatic objects, which take a wholly
	   different path - stores into the frame rather than a data
	   stream. */
	struct pt aflat[2];
	int agrid[2][3] = { 1, 2, 3, 4, 5, 6 };
	struct pt apart[2] = { { { 1 }, 2 }, { { 3, 4 } } };

	{
		struct pt tmp[] = { 1, 2, 3, 4, 10, 20, 30,
				    5, 6, 7, 8, 40, 50, 60 };
		aflat[0] = tmp[0];
		aflat[1] = tmp[1];
	}

	show("flat", flat, 2);
	show("braced", braced, 2);
	show("mixed", mixed, 2);
	show("part", part, 2);
	show("aflat", aflat, 2);
	show("apart", apart, 2);

	for (i = 0; i < 2; i++)
		for (j = 0; j < 3; j++)
			printf("grid %d %d %d %d\n", i, j,
				grid[i][j], grid2[i][j]);
	for (i = 0; i < 3; i++)
		printf("part2 %d %d %d\n", i, part2[i][0], part2[i][1]);
	for (i = 0; i < 2; i++)
		for (j = 0; j < 3; j++)
			printf("agrid %d %d %d\n", i, j, agrid[i][j]);

	printf("deep");
	for (i = 0; i < 4; i++)
		printf(" %d", deep[0].p.c[i]);
	printf(" %d %d %d %d\n", deep[0].p.b, deep[0].p.e, deep[0].p.k,
		deep[0].tail);

	printf("sizes %d %d %d\n", (int)(sizeof(flat) / sizeof(flat[0])),
		(int)(sizeof(grid) / sizeof(grid[0])),
		(int)(sizeof(deep) / sizeof(deep[0])));
	return 0;
}

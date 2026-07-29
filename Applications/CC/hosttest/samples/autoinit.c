/*
 * Initialisers for automatic aggregates.
 *
 * c-testsuite 00117, 00118 and 00185. Only statics and globals could be
 * initialised as aggregates; "int x[] = { 1, 0 };" inside a function
 * was rejected outright with "not a valid auto initializer". This is
 * everyday C and its absence is felt immediately.
 *
 * The interesting parts are the offsets and the padding. An automatic
 * aggregate occupies a contiguous run of the frame, so an element is
 * the symbol's own local with the member offset added; and C requires
 * everything the initialiser does not mention to be zero, which for a
 * stack object has to be written rather than assumed.
 */

int printf();

struct point {
	int x;
	int y;
};

struct mixed {
	char c;
	int i;
	char d;
};

struct nest {
	struct point p;
	int tag;
};

union u {
	int i;
	char c[4];
};

int main(void)
{
	/* unsized array, exactly filled */
	int a[] = { 1, 0, 3 };

	/* sized array, exactly filled */
	int b[4] = { 10, 20, 30, 40 };

	/* sized array, partly filled - the rest must be zero */
	int c[6] = { 5, 6 };

	/* a whole array of zeroes written the usual way */
	int z[5] = { 0 };

	/* char array, so the padding is not word sized */
	char cb[7] = { 'a', 'b' };

	/* struct, fully and partly */
	struct point p1 = { 3, 4 };
	struct point p2 = { 7 };

	/* struct with holes in it from alignment */
	struct mixed m = { 'x', 99 };

	/* nested */
	struct nest n = { { 11, 22 }, 33 };
	struct nest n2 = { { 44 } };

	/* array of structs */
	struct point ap[3] = { { 1, 2 }, { 3, 4 } };

	/* two dimensional */
	int two[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
	int part[3][2] = { { 9, 8 } };

	/* union - only the first member initialises */
	union u uu = { 0x41424344 };

	/* trailing comma */
	int tc[3] = { 1, 2, 3, };

	/* initialised from expressions, not just constants */
	int k = 4;
	int ex[3] = { k, k * 2, k + 100 };

	int i, j;

	printf("a %d %d %d\n", a[0], a[1], a[2]);
	printf("b %d %d %d %d\n", b[0], b[1], b[2], b[3]);
	printf("c %d %d %d %d %d %d\n", c[0], c[1], c[2], c[3], c[4], c[5]);
	printf("z %d %d %d %d %d\n", z[0], z[1], z[2], z[3], z[4]);

	printf("cb");
	for (i = 0; i < 7; i++)
		printf(" %d", (int) cb[i]);
	printf("\n");

	printf("p %d %d %d %d\n", p1.x, p1.y, p2.x, p2.y);
	printf("m %d %d %d\n", (int) m.c, m.i, (int) m.d);
	printf("n %d %d %d\n", n.p.x, n.p.y, n.tag);
	printf("n2 %d %d %d\n", n2.p.x, n2.p.y, n2.tag);

	printf("ap");
	for (i = 0; i < 3; i++)
		printf(" %d %d", ap[i].x, ap[i].y);
	printf("\n");

	printf("two");
	for (i = 0; i < 2; i++)
		for (j = 0; j < 3; j++)
			printf(" %d", two[i][j]);
	printf("\n");

	printf("part");
	for (i = 0; i < 3; i++)
		for (j = 0; j < 2; j++)
			printf(" %d", part[i][j]);
	printf("\n");

	printf("u %d\n", uu.i);
	printf("tc %d %d %d\n", tc[0], tc[1], tc[2]);
	printf("ex %d %d %d\n", ex[0], ex[1], ex[2]);

	/* initialisers run each time the block is entered */
	for (i = 0; i < 2; i++) {
		int fresh[3] = { 1, 2 };
		fresh[2] = 99;
		printf("fresh %d %d %d\n", fresh[0], fresh[1], fresh[2]);
	}

	return 0;
}

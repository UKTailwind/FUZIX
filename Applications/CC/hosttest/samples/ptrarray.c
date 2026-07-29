/*
 * Pointers to arrays, multidimensional arrays, and sizeof.
 *
 * c-testsuite 00130 and 00038.
 *
 * "char (*p)[4]" is a pointer *to* an array, not an array. In this
 * compiler's type encoding both are C_ARRAY and the difference is that
 * a pointer has more indirections than the type has dimensions - so
 * make_rval treated p as an array object, whose value is its own
 * address, and never loaded it. p[1][3] then read from the address of
 * p rather than from what p pointed at, and quietly returned 0.
 *
 * sizeof is here because it has the same flavour of bug: "sizeof 0 < 2"
 * is "(sizeof 0) < 2", and parsing a whole expression after sizeof made
 * it "sizeof (0 < 2)" - always 4, so always true.
 */

int printf();

int arr2[3][4];
char carr[2][4];

int main(void)
{
	char arr[2][4], (*p)[4], *q;
	int (*ip)[4];
	int v[4];
	int x, *xp;
	int i, j;

	/* the reduced case from 00130 */
	p = arr;
	q = &arr[1][3];
	arr[1][3] = 2;
	v[0] = 2;
	printf("a %d %d %d %d\n", arr[1][3], p[1][3], *q, *v);

	/* every element, through the array and through the pointer */
	for (i = 0; i < 2; i++)
		for (j = 0; j < 4; j++)
			arr[i][j] = i * 4 + j;
	printf("b");
	for (i = 0; i < 2; i++)
		for (j = 0; j < 4; j++)
			printf(" %d", p[i][j]);
	printf("\n");

	/* an int array, so the scale is not 1 and a wrong one shows */
	ip = arr2;
	for (i = 0; i < 3; i++)
		for (j = 0; j < 4; j++)
			arr2[i][j] = i * 100 + j;
	printf("c %d %d %d\n", ip[0][1], ip[1][2], ip[2][3]);

	/* pointer arithmetic on a pointer to array */
	ip = arr2;
	ip++;
	printf("d %d %d\n", ip[0][0], ip[1][1]);
	printf("e %d\n", (int) (&arr2[2] - arr2));

	/* a global 2D char array through a pointer.
	   Declared then assigned, not initialised: "char (*cp)[4] = carr"
	   is still rejected as a type mismatch, because a decayed
	   "char[2][4]" and a declared "char (*)[4]" get different type
	   codes even though they are the same type. Recorded in
	   PLAN-conformance.md. */
	{
		char (*cp)[4];
		cp = carr;
		carr[1][2] = 'z';
		printf("f %d\n", (int) cp[1][2]);
	}

	/* sizeof, parenthesised and not */
	xp = &x;
	printf("g %d %d %d %d\n", (int) (sizeof(0) < 2), (int) (sizeof 0 < 2),
	       (int) (sizeof(char) < 1), (int) (sizeof(&x) != sizeof xp));
	/* sizeof p is a pointer and differs between the oracle and the
	   target, so compare it rather than printing it */
	printf("h %d %d %d\n", (int) sizeof arr, (int) sizeof arr[0],
	       (int) (sizeof p == sizeof(char *)));
	printf("i %d %d\n", (int) sizeof arr2, (int) sizeof arr2[0]);

	return 0;
}

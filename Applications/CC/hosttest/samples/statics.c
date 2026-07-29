/*
 * Static locals, switch labels that do not sit in a block, and null
 * pointer comparisons.
 *
 * c-testsuite 00182, 00051 and 00112. Three unrelated faults, grouped
 * because each is small and all three were silent.
 *
 *  * A numbered label emitted into bss - which is how a static local is
 *    written out - was given a *data* address, so "static int d[4]"
 *    inside a function aliased the string literal area. Assigning to
 *    d[3] rewrote the fourth word of the literals and a later printf
 *    format string turned into whatever had been stored.
 *
 *  * "case X:" was treated as a complete statement, so a switch whose
 *    body was a bare statement rather than a block ended at the colon.
 *    The case body landed after the break label and ran whether or not
 *    the case matched.
 *
 *  * The data segment started at address 0, so the first object in it -
 *    a string literal - was indistinguishable from a null pointer.
 */

int printf();

int x = 0;

/* static local array: must not alias the literals */
int sa(void)
{
	static int d[4];
	d[0] = 7;
	d[3] = 9;
	return d[0] * 10 + d[3];
}

/* static locals keep their value across calls */
int counter(void)
{
	static int n;
	static int m = 100;
	n++;
	m += 2;
	return n * 1000 + m;
}

/* several statics in one function, and one initialised */
int mixed(void)
{
	static char buf[8];
	static int init = 5;
	int i;
	for (i = 0; i < 7; i++)
		buf[i] = 'a' + i;
	buf[7] = 0;
	return init + buf[6];
}

/* switch whose body is a single statement, case taken */
int taken(void)
{
	switch (x)
		case 0:
			return 10;
	return 1;
}

/* ... and not taken: must fall past the switch */
int nottaken(void)
{
	switch (x)
		case 1:
			return 1;
	return 30;
}

/* a nested switch as the body of a case, with a goto out of it */
int nested(void)
{
	switch (x)
		case 0:
			switch (x) {
			case 0:
				goto out;
			default:
				return 1;
			}
out:
	return 20;
}

/* a case buried inside a nested block */
int buried(void)
{
	switch (x) {
		{
			case 0:
				return 40;
		}
	}
	return 1;
}

/* consecutive labels on one statement */
int runof(void)
{
	switch (x) {
	case 3:
	case 0:
	case 4:
		return 50;
	default:
		return 1;
	}
}

char *g = "xyz";

int main(void)
{
	printf("sa %d\n", sa());
	printf("count %d %d %d\n", counter(), counter(), counter());
	printf("mixed %d\n", mixed());
	printf("literals [%s] [%s]\n", "first", "second");

	printf("sw %d %d %d %d %d\n", taken(), nottaken(), nested(),
	       buried(), runof());

	printf("null %d %d %d %d\n", "abc" == (void *) 0,
	       "abc" != (void *) 0, g == (void *) 0, g != 0);
	return 0;
}

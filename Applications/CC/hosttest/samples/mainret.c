/*
 * printf's return value.
 *
 * printf returns the number of characters written. It used to return 0
 * here, and the reason is worth recording: main had no implicit
 * "return 0", so it handed back whatever was left in the accumulator.
 * The last thing a program does is usually a printf, so making printf
 * return a count made programs exit with the number of characters they
 * had printed - it broke two conformance tests on the spot. The fault
 * was in main, not in printf.
 *
 * cc1 now gives main that implicit return. It recognises main by symbol
 * id: cc0 interns "main" first so it always carries T_MAIN, because
 * names reach cc1 as ids and never as strings.
 *
 * That part is NOT tested here, and cannot be: gcc with -std=gnu89 -
 * which optest.sh must use for the rest of the dialect - does not zero
 * main's return either, so it produces the same wrong answer we used
 * to. Zeroing it is a C99 rule that every modern compiler applies. The
 * coverage for it is c-testsuite 00206 and 00212, which run the program
 * and require exit 0; both failed with exit 12 and exit 3 before the
 * fix. This file ends with an explicit "return 0" so gcc can be an
 * oracle for everything else in it.
 */

int printf();

int counted(void)
{
	/* printf returns the number of characters written */
	return printf("hello\n");
}

int main(void)
{
	int n;

	n = counted();
	printf("count %d\n", n);

	/* a longer one, and one with a conversion in it */
	n = printf("0123456789\n");
	printf("count %d\n", n);
	n = printf("%d %s\n", 42, "xy");
	printf("count %d\n", n);

	/* the empty case */
	n = printf("");
	printf("count %d\n", n);

	return 0;
}

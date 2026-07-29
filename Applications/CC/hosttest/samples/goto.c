/*
 * goto and labels.
 *
 * The backend keyed labels on the first three characters of their
 * name. Its own loop labels are "_b", "_c", "_e" and so on and were
 * fine; a user label is "_g<symbol number>", so "_g32769", "_g32770"
 * and "_g32771" were all "_g3" - every goto label in a function was
 * the same label and the last one defined won. Two labels in one
 * function was enough to produce a jump to itself.
 *
 * So the point here is *several* labels per function, and jumps that
 * go both forwards and backwards between them.
 */

int printf();

/* the c-testsuite reproducer: labels that fall together, and a
   label immediately followed by another */
int chain(void)
{
start:
	goto next;
	return 1;
success:
	return 0;
next:
foo:
	goto success;
	return 1;
}

/* backwards jumps, and a label used as a loop */
int countdown(int n)
{
	int total = 0;
again:
	if (n <= 0)
		goto out;
	total += n;
	n--;
	goto again;
out:
	return total;
}

/* many labels in one function, jumped to out of order */
int scatter(int n)
{
	int r = 0;

	if (n == 0)
		goto zero;
	if (n == 1)
		goto one;
	if (n == 2)
		goto two;
	goto other;
zero:
	r = 100;
	goto done;
one:
	r = 200;
	goto done;
two:
	r = 300;
	goto done;
other:
	r = 400;
done:
	return r;
}

/* goto out of a nested loop, the usual real reason for using it */
int escape(void)
{
	int i, j;
	for (i = 0; i < 10; i++) {
		for (j = 0; j < 10; j++) {
			if (i * j == 42)
				goto found;
		}
	}
	return -1;
found:
	return i * 100 + j;
}

/* a label at the very end, and a forward jump over declarations */
int tail(int n)
{
	if (n)
		goto fin;
	n = 99;
fin:
	return n;
}

int main(void)
{
	printf("chain %d\n", chain());
	printf("countdown %d\n", countdown(5));
	printf("scatter %d %d %d %d\n", scatter(0), scatter(1), scatter(2),
	       scatter(3));
	printf("escape %d\n", escape());
	printf("tail %d %d\n", tail(0), tail(7));
	return 0;
}

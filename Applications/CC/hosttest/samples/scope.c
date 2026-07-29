/*
 * Block scope. Not in the suite yet - this is the probe for what the
 * front end does and does not do, so a plan can be based on measured
 * behaviour rather than on reading the parser.
 */

int printf();

int shadow_outer(void)
{
	int x;
	int total;

	x = 1;
	total = 0;
	{
		int x;		/* shadows the outer x */
		x = 10;
		total += x;
	}
	total += x;		/* must see 1 again */
	return total;		/* 11 */
}

int sibling_blocks(void)
{
	int total;

	total = 0;
	{
		int a;
		a = 3;
		total += a;
	}
	{
		int a;		/* same name, different block */
		a = 4;
		total += a;
	}
	return total;		/* 7 */
}

int nested(void)
{
	int i;
	int total;

	total = 0;
	for (i = 0; i < 3; i++) {
		int j;
		j = i * 2;
		{
			int k;
			k = j + 1;
			total += k;
		}
	}
	return total;		/* 1+3+5 = 9 */
}

int param_shadow(int n)
{
	{
		int n;		/* shadows the parameter */
		n = 100;
		if (n != 100)
			return -1;
	}
	return n;		/* the parameter again */
}

int main(void)
{
	printf("shadow %d\n", shadow_outer());
	printf("sibling %d\n", sibling_blocks());
	printf("nested %d\n", nested());
	printf("param %d\n", param_shadow(5));
	return 0;
}

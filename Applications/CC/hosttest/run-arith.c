/* Order-sensitive: sub() proves argument mapping, loop proves control
   flow, and the return value is checkable from the shell. */
int sub(int a, int b)
{
	return a - b;
}

int main(void)
{
	int i;
	int total;

	total = 0;
	for (i = 1; i <= 10; i++)
		total = total + i;
	/* 55 - 13 = 42 */
	return sub(total, 13);
}

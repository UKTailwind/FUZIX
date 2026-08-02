int printf();

/* The first program compiled on the machine itself. */
int main(void)
{
	int i, s;

	s = 0;
	for (i = 1; i <= 10; i++)
		s += i * i;
	printf("hello from the PC3, sum of squares = %d\n", s);
	return 0;
}

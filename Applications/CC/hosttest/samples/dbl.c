/*
 * Double arithmetic. This is the test for step 3 of the plan - the
 * float and double opcodes - and does not pass yet: cc0 encodes the
 * literals correctly but the backend has no way to add two of them.
 * It is here so that step 3 has something to aim at.
 *
 * Everything prints as an integer, because bcrun's printf has no %f.
 */

int printf();

double g = 2.5;
double zero = 0.0;

double half(double x)
{
	return x / 2.0;
}

int main(void)
{
	double a, b;
	int i;

	printf("size %d\n", (int) sizeof(double));

	a = 1.5;
	b = a + g;
	printf("add %d\n", (int) (b * 100.0));

	b = a * g;
	printf("mul %d\n", (int) (b * 100.0));

	b = g - a;
	printf("sub %d\n", (int) (b * 1000.0));

	b = a / g;
	printf("div %d\n", (int) (b * 1000.0));

	printf("neg %d\n", (int) (-a * 10.0));

	/* Conversions both ways */
	i = 7;
	a = i;
	printf("i2d %d\n", (int) (a * 3.0));
	a = 9.99;
	printf("d2i %d\n", (int) a);

	/* Comparisons, including the one that catches a bit pattern
	   compare pretending to be a float compare */
	printf("cmp %d %d %d %d\n", 1.5 < 2.5, 2.5 < 1.5, g == 2.5, g != 2.5);
	printf("zero %d %d\n", zero == 0.0, !zero);

	/* Precision the old single precision encoder could not hold */
	a = 0.123456789123456;
	printf("prec %d\n", (int) (a * 1000000000.0));

	/* And a function call, so doubles have to survive the argument
	   and return paths */
	printf("call %d\n", (int) (half(5.0) * 100.0));

	return 0;
}

/*
 * Floating point beyond the basics in dbl.c: float as well as double,
 * the conversion matrix in both directions and both signednesses,
 * compound assignment, and floats in arrays and structs.
 *
 * Everything prints as an integer, because bcrun's printf has no %f.
 * Scaling by a power of ten and truncating is enough to catch a wrong
 * result without depending on the last bit of a decimal conversion.
 */

int printf();

double gd = 2.5;
float gf = 1.25;
double darr[4];

struct point {
	double x;
	float y;
	int tag;
};

struct point origin;

double half(double x)
{
	return x / 2.0;
}

float fhalf(float x)
{
	return x / 2.0f;
}

double sum4(double a, double b, double c, double d)
{
	return a + b + c + d;
}

int main(void)
{
	double d, e;
	float f, g;
	int i;
	unsigned u;
	long long ll;
	char c;
	short s;

	printf("sizes %d %d\n", (int) sizeof(double), (int) sizeof(float));

	/* float arithmetic, which is not double arithmetic rounded */
	f = 1.5f;
	g = 0.25f;
	printf("f %d %d %d %d\n", (int) ((f + g) * 100.0f),
	       (int) ((f - g) * 100.0f), (int) ((f * g) * 100.0f),
	       (int) ((f / g) * 100.0f));
	printf("fneg %d\n", (int) (-f * 10.0f));
	printf("fcmp %d %d %d %d %d %d\n", f < g, f > g, f <= g, f >= g,
	       f == g, f != g);

	/* double and float meeting each other */
	d = f;			/* widen */
	printf("f2d %d\n", (int) (d * 100.0));
	g = 3.5;		/* narrow */
	printf("d2f %d\n", (int) (g * 100.0f));

	/* integers to floating, signed and unsigned */
	i = -7;
	u = 4000000000U;
	d = i;
	printf("i2d %d\n", (int) (d * 10.0));
	d = u;
	printf("u2d %d\n", (int) (d / 1000000.0));
	f = i;
	printf("i2f %d\n", (int) (f * 10.0f));

	/* floating back to integers, including the narrow ones */
	d = 9.99;
	i = d;
	c = d;
	s = d;
	printf("d2i %d %d %d\n", i, (int) c, (int) s);
	d = -9.99;
	i = d;
	printf("d2i- %d\n", i);
	f = 260.5f;
	c = f;			/* truncates, then narrows to a char */
	printf("f2c %d\n", (int) c);

	/* long long both ways */
	ll = 5000000000LL;
	d = ll;
	printf("ll2d %d\n", (int) (d / 1000000.0));
	d = 3000000000.0;
	ll = d;
	printf("d2ll %d %d\n", (int) (ll >> 32), (int) (ll & 0xFFFFFFFFL));

	/* compound assignment and the increment forms */
	d = 10.0;
	d += 2.5;
	printf("pluseq %d\n", (int) (d * 10.0));
	d -= 0.5;
	printf("minuseq %d\n", (int) (d * 10.0));
	d *= 2.0;
	printf("muleq %d\n", (int) (d * 10.0));
	d /= 4.0;
	printf("diveq %d\n", (int) (d * 10.0));
	f = 1.5f;
	f += 0.25f;
	printf("fpluseq %d\n", (int) (f * 100.0f));

	/* globals, arrays and structs */
	printf("glob %d %d\n", (int) (gd * 10.0), (int) (gf * 100.0f));
	for (i = 0; i < 4; i++)
		darr[i] = i / 4.0;
	printf("arr %d %d %d %d\n", (int) (darr[0] * 100.0),
	       (int) (darr[1] * 100.0), (int) (darr[2] * 100.0),
	       (int) (darr[3] * 100.0));
	origin.x = 1.5;
	origin.y = 2.5f;
	origin.tag = 7;
	printf("struct %d %d %d\n", (int) (origin.x * 10.0),
	       (int) (origin.y * 10.0f), origin.tag);

	/* arguments and returns, including several in a row */
	printf("call %d %d\n", (int) (half(5.0) * 100.0),
	       (int) (fhalf(5.0f) * 100.0f));
	printf("call4 %d\n", (int) (sum4(1.5, 2.25, 3.125, 4.0) * 1000.0));

	/* truth, which is not the same as the bit pattern being zero */
	d = 0.0;
	e = -0.0;
	printf("truth %d %d %d %d\n", !d, !e, d != 0.0, gd != 0.0);
	if (gd)
		printf("if-true\n");
	if (!d)
		printf("if-false\n");

	/* precision the old encoder could not hold */
	d = 0.123456789123456;
	printf("prec %d\n", (int) (d * 1000000000.0));

	return 0;
}

/*
 * Floating constant folding, and the float-literal bug it replaced.
 *
 * FLOAT (0x80) passed IS_INTORPTR() (< 0x87), so a pair of float
 * literals fell into constify()'s integer switch and was folded as two
 * IEEE754 bit patterns: 1.5f + 2.5f became 0x3FC00000 + 0x40200000 =
 * 0x7FE00000, a NaN; 2.0f * 0.5f overflowed to 0.0f; and a comparison
 * of two negative float constants came out backwards because negative
 * floats order in reverse as integers. DOUBLE (0x90) failed the same
 * test, so double constant arithmetic was never folded at all - every
 * use of a #define like (0.5 * PI) was a runtime multiply.
 *
 * Both now go through fold_float_binary(): + - * / and the relational
 * quartet fold by value; NaN, infinity, denormal operands or results,
 * and division by a floating zero are left to runtime, where the
 * board's DCP arithmetic is the authority (it flushes denormals, and
 * NaN canonical forms are its own business).
 *
 * gcc is the oracle: every line below must match the native build, and
 * would have differed under the old folder.
 */

int printf();

static float sf = 1.5f + 2.5f;			/* initialiser path */
static double sd = 180.0 / 3.141592653589793;

int main(void)
{
	float x = 1.5f + 2.5f;
	float y = 2.0f * 0.5f;
	float z = 7.5f - 0.25f;
	float q = 10.0f / 4.0f;
	double d = 0.5 * 3.141592653589793;
	double m = -1.5 * -2.0;

	/* float arithmetic folds, by value */
	printf("%d\n", x == 4.0f);
	printf("%d\n", y == 1.0f);
	printf("%d\n", z == 7.25f);
	printf("%d\n", q == 2.5f);
	printf("%d\n", sf == 4.0f);

	/* float compares fold, both signs */
	printf("%d\n", -1.0f < -2.0f);
	printf("%d\n", -1.0f > -2.0f);
	printf("%d\n", 1.0f < 2.0f);
	printf("%d\n", 0.5f <= 0.5f);
	printf("%d\n", 0.5f >= 0.75f);

	/* double arithmetic folds now, and to the same bits the runtime
	   computes */
	printf("%d\n", d == 1.5707963267948966);
	printf("%d\n", m == 3.0);
	printf("%d\n", sd > 57.0);
	printf("%d\n", sd < 57.3);

	/* double compares, both signs */
	printf("%d\n", -1.0 < -2.0);
	printf("%d\n", -1.0 >= -2.0);

	/* float promotes to double and folds at double */
	printf("%d\n", 1.5f + 2.5 == 4.0);

	/* a folded comparison is restamped with the operand's floating
	   type by the expression builder, so it must carry a floating
	   1.0 - integer 1 under DOUBLE reads back as a denormal, which
	   the host's arithmetic kept and the DCP flushed to zero */
	printf("%d\n", (1.0 < 2.0) * 1e300 == 1e300);
	printf("%d\n", (2.0 < 1.0) * 1e300 == 0.0);

	/* the refusals: all of these stay runtime and still agree with
	   the oracle */
	printf("%d\n", 1.0 / 0.0 > 1e300);	/* infinity */
	printf("%d\n", 1e-310 + 1e-310 > 0.0);	/* denormals */
	printf("%d\n", 1e300 * 1e300 > 1e300);	/* overflow to inf */
	return 0;
}

/* tanh(x) = sign(x) * (1 - 2/(e^{2|x|} + 1)), computed through expm1
 * so that small arguments keep their precision:
 *
 *     t = expm1(2|x|),  tanh = t / (t + 2)
 *
 * Beyond |x| ~ 19 the true value is 1 to more than double precision,
 * so it is clamped rather than computed.  Not musl's bit-exact
 * version - this libc had sinh and cosh's dependencies but no tanh
 * at all, and this form is accurate to the last digit or so across
 * the range, which is what the callers (the PC3 bytecode runtime's
 * MATH(TANH ...)) need.
 */
#include <math.h>

double tanh(double x)
{
	double ax = fabs(x);
	double t;

	if (ax > 20.0)
		t = 1.0;
	else {
		t = expm1(2.0 * ax);
		t = t / (t + 2.0);
	}
	return x < 0 ? -t : t;
}

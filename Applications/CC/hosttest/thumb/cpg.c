/* CP-G: floating point - arithmetic and compares through helper_op,
   the inline sign-flip negates and bit-test truthiness, and every
   conversion pair.  The truthiness shapes are the denormal-lesson
   regression: compare results BOOL'd, if(double), !double, -0.0. */
#include <stdio.h>

double dadd(double a, double b) { return a + b; }
double dsub(double a, double b) { return a - b; }
double dmul(double a, double b) { return a * b; }
double ddiv(double a, double b) { return a / b; }
double dneg(double a) { return -a; }
int dlt(double a, double b) { return a < b; }
int dle(double a, double b) { return a <= b; }
int dgt(double a, double b) { return a > b; }
int dge(double a, double b) { return a >= b; }
int deq(double a, double b) { return a == b; }
int dne(double a, double b) { return a != b; }
int dbool(double a) { return !!a; }
int dlnot(double a) { return !a; }

float fadd(float a, float b) { return a + b; }
float fsub(float a, float b) { return a - b; }
float fmul(float a, float b) { return a * b; }
float fdiv(float a, float b) { return a / b; }
float fneg(float a) { return -a; }
int flt(float a, float b) { return a < b; }
int feq(float a, float b) { return a == b; }
int fbool(float a) { return !!a; }
int flnot(float a) { return !a; }

double i2d(int a) { return a; }
double u2d(unsigned a) { return a; }
double ll2d(long long a) { return (double)a; }
int d2i(double a) { return (int)a; }
long long d2ll(double a) { return (long long)a; }
unsigned d2u(double a) { return (unsigned)a; }
float i2f(int a) { return (float)a; }
int f2i(float a) { return (int)a; }
double f2d(float a) { return a; }
float d2f(double a) { return (float)a; }

/* condition shapes: compare results consumed as truth, both ways */
int inrange(double x, double lo, double hi) { return x >= lo && x <= hi; }
int outside(double x, double lo, double hi) { return x < lo || x > hi; }
double clamp(double x, double lo, double hi)
{
	if (x < lo)
		return lo;
	if (x > hi)
		return hi;
	return x;
}

/* iterative double math: Leibniz pi and a Newton square root */
double pi(int n)
{
	double s = 0.0;
	double sign = 1.0;
	int i;
	for (i = 0; i < n; i++) {
		s += sign / (2 * i + 1);
		sign = -sign;
	}
	return 4.0 * s;
}

double newton(double x)
{
	double g = x;
	int i;
	for (i = 0; i < 40; i++)
		g = 0.5 * (g + x / g);
	return g;
}

/* double compound assignment - the helper_eqop path */
double dsum(int n)
{
	double s = 0.0;
	double step = 0.125;
	int i;
	for (i = 0; i < n; i++)
		s += step;
	return s;
}

double dscale(int n)
{
	double s = 65536.0;
	int i;
	for (i = 0; i < n; i++)
		s /= 2.0;
	return s;
}

int main(void)
{
	double a = 3.5, b = -1.25;
	float fa = 2.5f, fb = -0.5f;
	double zero = 0.0;
	double mzero;

	mzero = -zero;
	printf("%.6f %.6f %.6f %.12f\n", dadd(a, b), dsub(a, b),
	       dmul(a, b), ddiv(a, b));
	printf("%.6f %.6f\n", dneg(a), dneg(mzero));
	printf("%d%d%d%d%d%d", dlt(b, a), dle(a, a), dgt(a, b),
	       dge(b, a), deq(a, a), dne(a, b));
	printf(" %d%d%d%d%d%d\n", dlt(a, b), dle(a, b), dgt(b, a),
	       dge(b, a), deq(a, b), dne(a, a));
	printf("%d %d %d %d %d %d\n", dbool(a), dbool(zero), dbool(mzero),
	       dlnot(a), dlnot(zero), dlnot(mzero));
	printf("%.6f %.6f %.6f %.6f %.6f\n", fadd(fa, fb), fsub(fa, fb),
	       fmul(fa, fb), fdiv(fa, fb), fneg(fa));
	printf("%d %d %d %d %d %d\n", flt(fb, fa), feq(fa, fa),
	       fbool(fa), fbool(0.0f), flnot(fb), flnot(0.0f));
	printf("%.6f %.6f %.6f\n", i2d(-42), u2d(0xFFFFFFFFu),
	       ll2d(123456789012345LL));
	printf("%d %lld %u\n", d2i(-3.99), d2ll(1e15), d2u(4000000000.0));
	printf("%.6f %d %.6f %.9f\n", i2f(-7), f2i(9.99f),
	       f2d(1.5f), d2f(1.0 / 3.0));
	printf("%d %d %d\n", inrange(0.5, 0.0, 1.0),
	       outside(1.5, 0.0, 1.0), inrange(1.5, 0.0, 1.0));
	printf("%.6f %.6f %.6f\n", clamp(5.0, 0.0, 1.0),
	       clamp(-5.0, 0.0, 1.0), clamp(0.25, 0.0, 1.0));
	printf("%.12f\n", pi(1000));
	printf("%.12f %.6f\n", newton(2.0), newton(1e10));
	printf("%.6f %.12f\n", dsum(1000), dscale(30));
	return 0;
}

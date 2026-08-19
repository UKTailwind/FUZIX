#ifndef _MATH_H
#define _MATH_H

/* Every one of these is a native function in the bytecode interpreter:
 * the call compiles to BC_LIBCALL and the arithmetic runs at machine
 * speed.  See lib_math() in bcrun.c.
 *
 * Full prototypes, not K&R empty parens, and it matters: pow(10, n)
 * with integer arguments must convert them to double at the call site.
 * Unprototyped, the ints go through as ints and the interpreter reads
 * 8 bytes where 4 were pushed. */

double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
double sqrt(double x);
double exp(double x);
double log(double x);
double log10(double x);
double pow(double x, double y);
double floor(double x);
double ceil(double x);
double fabs(double x);
double fmod(double x, double y);

#endif

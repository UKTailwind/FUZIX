#ifndef _MATH_H
#define _MATH_H

/*
 * The maths functions bcrun has (its mfns table), in double precision.
 * On the CMM2 they are the C library's, in the M7's FPU.
 *
 * Prototyped, unlike ctest-include's declarations: sqrt(2) has to pass
 * 2.0, and only a prototype makes the compiler convert it.
 */

#define M_PI	3.14159265358979323846
#define M_E	2.71828182845904523536

double sin(double);
double cos(double);
double tan(double);
double asin(double);
double acos(double);
double atan(double);
double sinh(double);
double cosh(double);
double tanh(double);
double sqrt(double);
double exp(double);
double log(double);
double log10(double);
double floor(double);
double ceil(double);
double fabs(double);
double pow(double, double);
double atan2(double, double);
double fmod(double, double);

#endif

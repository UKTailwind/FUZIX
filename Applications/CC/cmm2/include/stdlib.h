#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

/*
 * ctest-include's stdlib.h with the rest of what bcrun has: the number
 * conversions, labs and llabs, and rand.  Without a declaration atof()
 * and strtod() would be taken to return an int.
 */

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 2147483647

void *malloc();
void *calloc();
void *realloc();
void free();
void exit();
int atoi();
long atol();
double atof();
double strtod();
long strtol();
unsigned long strtoul();
int abs();
long labs();
long long llabs();
int rand();
void srand();

#endif

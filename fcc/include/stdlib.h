#ifndef _STDLIB_H
#define _STDLIB_H

/* Shadows the compiler test suite's stdlib.h: the same set plus the
 * conversions and the pseudo-random pair, all of which are native
 * functions in the bytecode interpreter. */

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#define RAND_MAX 0x7FFFFFFF

void *malloc(size_t n);
void *calloc(size_t n, size_t sz);
void *realloc(void *p, size_t n);
void free(void *p);
void exit(int status);
int atoi(const char *s);
long atol(const char *s);
double atof(const char *s);
int abs(int v);
long labs(long v);
long long llabs(long long v);

double strtod(const char *s, char **end);
long strtol(const char *s, char **end, int base);
long long strtoll(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
unsigned long long strtoull(const char *s, char **end, int base);

int rand(void);
void srand(unsigned seed);

#endif

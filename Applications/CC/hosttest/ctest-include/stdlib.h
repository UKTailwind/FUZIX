#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

void *malloc();
void *calloc();
void *realloc();
void free();
void exit();
int atoi();
int abs();

#endif

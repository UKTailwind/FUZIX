#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>

size_t strlen();
char *strcpy();
char *strncpy();
char *strcat();
int strcmp();
int strncmp();
char *strchr();
char *strrchr();

void *memset();
void *memcpy();
void *memmove();
int memcmp();

#endif

#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>

#define EOF (-1)

/*
 * A FILE * is the file descriptor plus one, so that stdin, stdout and
 * stderr can be constants rather than objects: the bytecode object
 * format can import a function from the runtime but not a variable, so
 * they could not be real ones. Programs never look inside a FILE, and
 * C says they may not, so an incomplete type is honest about it.
 */
typedef struct __file FILE;

#define stdin  ((FILE *)1)
#define stdout ((FILE *)2)
#define stderr ((FILE *)3)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int printf();
int sprintf();
int puts();
int putchar();

FILE *fopen();
int fclose();
size_t fread();
size_t fwrite();
int fgetc();
int getc();
int fputc();
int putc();
char *fgets();
int fputs();
int fprintf();
int feof();
int fseek();
long ftell();
void rewind();
int fflush();
int remove();

#endif

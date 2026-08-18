#ifndef UPNG_PC3_H
#define UPNG_PC3_H
/*
 * The four things upng.c asks of MMBasic, declared for a standalone
 * build.  They are all it asks for, which is why the decoder drops in
 * with two edited lines - see the note at the top of upng.c.
 *
 * The definitions are in upng_pc3.c, deliberately: as statics in this
 * header, upng.o and loadpng.o each got their own arena and claimed 2M
 * apiece.  One translation unit, one arena.
 */

/* MMBasic_Includes.h pulled this in; a standalone build must say so. */
#include "upng.h"

void *GetMemory(unsigned long n);
void FreeMemorySafe(void *pp);
void routinechecks(void);
void error(const char *msg);

#endif /* UPNG_PC3_H */

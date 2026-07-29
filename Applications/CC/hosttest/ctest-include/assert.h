#ifndef _ASSERT_H
#define _ASSERT_H

#include <stdio.h>
#include <stdlib.h>

/* bcrun has no abort(), so a failed assertion exits non-zero instead.
   The harness only cares that the program did not exit 0. */
#ifdef NDEBUG
#define assert(e) ((void)0)
#else
#define assert(e) ((e) ? (void)0 : (printf("assertion failed\n"), exit(1)))
#endif

#endif

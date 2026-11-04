#ifndef _TIME_H
#define _TIME_H

#include <stddef.h>

/*
 * time() is bcrun's: the seconds since 1970 by the CMM2's clock (DATE$
 * and TIME$).  For timing, clock() gives the microseconds since the
 * program started, and time_us() and time_us64() those since the CMM2
 * started (31 bits, and all 64).
 */

typedef long time_t;
typedef long clock_t;

#define CLOCKS_PER_SEC	1000000L

time_t time();
clock_t clock();
long time_us();
long long time_us64();

#endif

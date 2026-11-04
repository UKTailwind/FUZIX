#ifndef _TIME_H
#define _TIME_H

#include <stddef.h>

/*
 * time() is bcrun's: the seconds since 1970 by the CMM2's clock (DATE$
 * and TIME$).  For timing, time_us() and time_us64() give the
 * microseconds since the CMM2 started (31 bits, and all 64).
 */

typedef long time_t;

time_t time();
long time_us();
long long time_us64();

#endif

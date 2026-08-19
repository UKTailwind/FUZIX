#ifndef _TIME_H
#define _TIME_H

typedef long time_t;

/* Seconds since the epoch, via the interpreter.  The runtime does its
 * own calendar arithmetic (mm_civil_from_days), so there is no struct
 * tm and no gmtime here. */
time_t time(time_t *out);

#endif

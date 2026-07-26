/* Force-included on Fuzix builds (see Makefile.armm0): declarations
 * the Fuzix libc headers are missing. */
#ifndef FUZIXSHIM_H
#define FUZIXSHIM_H
typedef int timer_t ;
extern long long llabs (long long) ;
extern int remove (const char *) ;
extern unsigned long long strtoull (const char *, char **, int) ;
#endif

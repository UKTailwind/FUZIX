/* Copyright (C) 1995,1996 Robert de Bath <rdebath@cix.compulink.co.uk>
 * This file is part of the Linux-8086 C library and is distributed
 * under the GNU Library General Public License.
 *
 * FIXME: helper for stdio needs to become re-entrant
 */

#include <string.h>

char * __ultostr_r(char *buf, unsigned long val, int radix)
{
   register char *p;
   register int c;
   register int rdbase = 'A' - 10;

   /* Caller wants us to use capitals */
   if (radix < 0) {
    rdbase = 'a' - 10;
    radix = -radix;
   }
   if (radix > 36 || radix < 2 ) return 0;

   p = buf + 34;
   *--p = '\0';

   do
   {
      c = val % radix;
      val /= radix;
      if( c > 9 ) *--p = rdbase + c; else *--p = '0'+c;
   }
   while(val);
   return p;
}

char * __ltostr_r(char *buf, long val, int radix)
{
   char *p;
   int flg = 0;
   if( val < 0 ) { flg++; val= -val; }
   p = __ultostr_r(buf, val, radix);
   if(p && flg) *--p = '-';
   return p;
}

#ifdef CONFIG_PRINTF_LONGLONG
/* The same pair for long long, for the targets that ask for %ll.
 *
 * Optional because it is not free: a 64-bit divide is a libgcc call on
 * every machine Fuzix runs on, and on the 8-bit ones it is a great deal
 * more than that.  A target that never prints a 64-bit value should not
 * carry it, which is why this is a config switch and not simply always
 * here - see vfprintf.c, where "%ll" without it is a compile-time
 * error rather than a silent wrong answer. */
char * __ulltostr_r(char *buf, unsigned long long val, int radix)
{
   register char *p;
   register int c;
   register int rdbase = 'A' - 10;

   if (radix < 0) {
    rdbase = 'a' - 10;
    radix = -radix;
   }
   if (radix > 36 || radix < 2 ) return 0;

   p = buf + 34;
   *--p = '\0';

   do
   {
      c = (int)(val % (unsigned)radix);
      val /= (unsigned)radix;
      if( c > 9 ) *--p = rdbase + c; else *--p = '0'+c;
   }
   while(val);
   return p;
}

char * __lltostr_r(char *buf, long long val, int radix)
{
   char *p;
   int flg = 0;
   if( val < 0 ) { flg++; val= -val; }
   p = __ulltostr_r(buf, val, radix);
   if(p && flg) *--p = '-';
   return p;
}
#endif

/* sadly we do not know the size of the user-provided buffer so we cannot write
   it backwards, so just copy the result from our own buffer whose size we know */

char *ultoa (unsigned long value, char *strP, int radix)
{
    char buf[34];
    return strcpy(strP, __ultostr_r(buf, value, radix));
}

char *ltoa (long value, char *strP, int radix)
{
    char buf[34];
    return strcpy(strP, __ltostr_r(buf, value, radix));
}

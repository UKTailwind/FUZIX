#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#define IntToStrBufSize 65
#define STR_AUTO_PRECISION 999
#define STR_FLOAT_PRECISION 998
#define STR_SIG_DIGITS 9
#define STR_FLOAT_DIGITS 6
#include "ref_body.c"

#include "mmb_runtime.h"

static int fails = 0, tests = 0;
static void cmpf(double v, int m, int n, char ch) {
    char a[256], b[256];
    FloatToStr(a, v, m, n, ch);
    mm_float_to_str(b, v, m, n, (unsigned char)ch);
    tests++;
    if (strcmp(a, b)) { fails++; printf("MISMATCH FloatToStr(%g,%d,%d,'%c'): ref=[%s] mine=[%s]\n", v, m, n, ch, a, b); }
}
static void cmpi(long long v, int pad, int maxch, int radix) {
    char a[256], b[256];
    IntToStrPad(a, v, (signed char)pad, maxch, radix);
    mm_int_to_str_pad(b, v, (signed char)pad, maxch, radix);
    tests++;
    if (strcmp(a, b)) { fails++; printf("MISMATCH IntToStrPad(%lld,%d,%d,%d): ref=[%s] mine=[%s]\n", v, pad, maxch, radix, a, b); }
}
/* mm_idiv/mm_mod take a 32-bit fast path when both values fit, because
   the M33 divides 32-bit integers in one instruction and 64-bit ones in
   a libgcc call.  The wide operation is the oracle: every pair must
   agree with it, including the ones that must NOT narrow. */
static void cmpdiv(long long a, long long b) {
    long long qref, rref;
    if (b == 0) return;
    if (a == (-9223372036854775807LL - 1) && b == -1) return;   /* UB */
    qref = a / b; rref = a % b;
    tests++;
    if (mm_idiv(a, b) != qref || mm_mod(a, b) != rref) {
        fails++;
        printf("MISMATCH %lld / %lld: ref=[%lld,%lld] mine=[%lld,%lld]\n",
               a, b, qref, rref, (long long)mm_idiv(a, b),
               (long long)mm_mod(a, b));
    }
}

int main(void) {
    double vals[] = {0, 1, -1, 0.5, -0.5, 1.0/3.0, 123.456, -123.456, 53, 1e-5, 1e-4,
                     9.99999e5, 1e6, 1.23456789e12, -1.23456789e-12, 3.14159265358979,
                     2.5, -2.5, 0.0001, 0.00009999, 1e15, 1e-300, 65536, 1e9+0.5, 0.1, 0.2, 0.7,
                     1234567890123.0, -0.000123456, 100000.0, 999999.4999, 6.02214076e23};
    int ms[] = {0, 1, -1, 6, -6, 12};
    int ns[] = {STR_AUTO_PRECISION, STR_FLOAT_PRECISION, 0, 2, 5, -5, 9};
    char chs[] = {' ', '0', '*'};
    for (unsigned i=0;i<sizeof vals/sizeof*vals;i++)
      for (unsigned a=0;a<sizeof ms/sizeof*ms;a++)
        for (unsigned b=0;b<sizeof ns/sizeof*ns;b++)
          for (unsigned c=0;c<sizeof chs;c++)
            cmpf(vals[i], ms[a], ns[b], chs[c]);
    long long ivals[] = {0,1,-1,7,-7,255,-255,1234567890123456789LL,-1234567890123456789LL,
                         (long long)0xFFFF0000FFFF0044ULL, 65535, -32768};
    int radix[] = {10, 16, 8, 2};
    for (unsigned i=0;i<sizeof ivals/sizeof*ivals;i++)
      for (unsigned a=0;a<sizeof ms/sizeof*ms;a++)
        for (unsigned r=0;r<4;r++)
          for (unsigned c=0;c<sizeof chs;c++)
            cmpi(ivals[i], chs[c], ms[a], radix[r]);
    /* the narrowing edges: either side of the 32-bit boundary, both
       signs, and -1 as a divisor (INT32_MIN / -1 does not narrow) */
    {
        long long dv[] = {1, -1, 2, -2, 7, -7, 10, -10, 255, -255, 3600,
                          2147483647LL, -2147483647LL, -2147483648LL,
                          2147483648LL, -2147483649LL, 4294967296LL,
                          1234567890123456789LL, -1234567890123456789LL,
                          -9223372036854775807LL - 1, 9223372036854775807LL};
        for (unsigned i = 0; i < sizeof dv / sizeof *dv; i++)
            for (unsigned j = 0; j < sizeof dv / sizeof *dv; j++)
                cmpdiv(dv[i], dv[j]);
        for (unsigned i = 0; i < sizeof dv / sizeof *dv; i++) {
            cmpdiv(0, dv[i]);
            cmpdiv(dv[i], 1);
            cmpdiv(dv[i], -1);
        }
    }
    printf("%d comparisons, %d mismatches\n", tests, fails);
    return fails != 0;
}

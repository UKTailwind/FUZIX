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
    printf("%d comparisons, %d mismatches\n", tests, fails);
    return fails != 0;
}

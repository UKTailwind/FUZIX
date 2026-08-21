#ifndef MMB_ARRAY_H
#define MMB_ARRAY_H
/*
 *	Whole-array operations, the dynamic DIM/REDIM arithmetic, and
 *	the MATH() array reductions.
 *
 *	    ARRAY SET v, a()            MATH SUM a()
 *	    ARRAY ADD in(), v, out()    MATH MEAN / SD / MEDIAN
 *	    ARRAY SLICE / INSERT        MATH MAX / MIN [, idx%]
 *	    DIM a(n) / REDIM            ERASE of a dynamic array
 *
 *	WHY A HEADER rather than the runtime - the mmb_math.h bargain.
 *	These were 26 wrappers and rows in bcrun's lookup table over
 *	1.8K of loops, carried by every program on the machine whether
 *	or not it ever touches a whole array; bcrun is the floor under
 *	what a BASIC program has left to work in.  Here the code lands
 *	only in a program that asked for it, and cc1's dead-static rule
 *	drops the variants it never names.  Moved out of mmb_runtime.c
 *	on 2026-08-21, byte for byte except that the MM_STAT_BODY macro
 *	is written out as plain functions - the board cpp is the least
 *	proven pass on the machine and a header must not lean on it.
 *
 *	mm_arr_count STAYS in the runtime: it is 44 bytes and the count
 *	expression of every array parameter goes through it, so it is
 *	already paid for everywhere.  SD and MEDIAN want sqrt, hence the
 *	include - the same libm every program already reaches.
 *
 *	SORT is mmb_sort.h; the componentwise MATH C_ADD family is
 *	mmb_math.h.  Three headers because the include is the
 *	granularity (see mmb2c.py's include block).
 */

#include <math.h>
#include <string.h>             /* memcpy, for REDIM PRESERVE */

#ifndef MMG_FN
#if defined(MM_FCC) || defined(MM_PC3)
#define MMG_FN static
#else
#define MMG_FN static __inline__ __attribute__((unused))
#endif
#endif

/*
 * DIM a(n) and REDIM [PRESERVE] a(n), where n is worked out while the
 * program runs.  The allocation and the free are emitted AROUND these
 * by the translator - under bcrun only a call made by the program
 * reaches the VM's allocator - so what is here is the arithmetic and
 * the copy, which are safe anywhere.  PRESERVE's only-the-last-index
 * rule is MMBasic's (cmd_redim): the elements are row-major, so
 * growing an earlier subscript would scramble them.
 */
MMG_FN unsigned long mm_arr_bytes(const MMINTEGER *nb, unsigned long elsize)
{
    int rank = (int)nb[0], k;

    if (rank < 1 || rank > MM_MAXDIM)
        MM_RAISEV("too many dimensions", 0);
    for (k = 1; k <= rank; k++)
        if (nb[k] < 0)
            MM_RAISEV("array bound cannot be negative", 0);
    return (unsigned long)mm_arr_count(nb) * elsize;
}

/* Put the new block in place and hand the old one back to be freed.
 * Returns the old pointer, or NULL if there was none. */
MMG_FN void *mm_arr_swap(void *old, MMINTEGER *b, const MMINTEGER *nb,
                         void *newblock, unsigned long elsize, int preserve)
{
    int rank = (int)nb[0];
    int oldn, newn, k;

    if (newblock == NULL)
        return old;                     /* the allocation already raised */
    if (preserve && old != NULL) {
        if ((int)b[0] != rank)
            MM_RAISEV("Only the last array index can be changed", old);
        for (k = 1; k < rank; k++)
            if (b[k] != nb[k])
                MM_RAISEV("Only the last array index can be changed",
                          old);
        newn = mm_arr_count(nb);
        oldn = mm_arr_count(b);
        if (oldn > newn)
            oldn = newn;
        memcpy(newblock, old, (size_t)oldn * elsize);
    }
    for (k = 0; k <= rank; k++)
        b[k] = nb[k];
    return old;
}

MMG_FN void mm_arr_set_i(MMINTEGER *a, int n, MMINTEGER v)
{ int i; for (i = 0; i < n; i++) a[i] = v; }

MMG_FN void mm_arr_set_f(MMFLOAT *a, int n, MMFLOAT v)
{ int i; for (i = 0; i < n; i++) a[i] = v; }

MMG_FN void mm_arr_set_s(char (*a)[MM_STRSZ], int n, const char *v)
{ int i; for (i = 0; i < n; i++) mm_sset(a[i], v); }

MMG_FN void mm_arr_add_i(const MMINTEGER *in, int n, MMINTEGER v,
                         MMINTEGER *out)
{ int i; for (i = 0; i < n; i++) out[i] = in[i] + v; }

MMG_FN void mm_arr_add_f(const MMFLOAT *in, int n, MMFLOAT v, MMFLOAT *out)
{ int i; for (i = 0; i < n; i++) out[i] = in[i] + v; }

MMG_FN void mm_arr_add_s(char (*in)[MM_STRSZ], int n, const char *v,
                         char (*out)[MM_STRSZ])
{
    int i;
    char t[MM_STRSZ];
    for (i = 0; i < n; i++) { mm_sset(t, mm_scat(in[i], v)); mm_sset(out[i], t); }
}

MMG_FN void mm_arr_scale_i(const MMINTEGER *in, int n, MMINTEGER v,
                           MMINTEGER *out)
{ int i; for (i = 0; i < n; i++) out[i] = in[i] * v; }

MMG_FN void mm_arr_scale_f(const MMFLOAT *in, int n, MMFLOAT v, MMFLOAT *out)
{ int i; for (i = 0; i < n; i++) out[i] = in[i] * v; }

/* ---- ARRAY SLICE / ARRAY INSERT: one strided copy serves both
 * directions and all three types.  `flat` is the one-dimensional
 * side's length and must equal n - MMBasic's "Size mismatch", made
 * here because a bound may not exist until the program runs. */

#define MM_SIZEMM "Size mismatch between slice and target array"

MMG_FN void mm_arr_copy_i(MMINTEGER *dst, int dstep, const MMINTEGER *src,
                          int sstep, int n, int flat)
{
    int i;
    if (flat != n) MM_RAISE(MM_SIZEMM);
    for (i = 0; i < n; i++) dst[i * dstep] = src[i * sstep];
}

MMG_FN void mm_arr_copy_f(MMFLOAT *dst, int dstep, const MMFLOAT *src,
                          int sstep, int n, int flat)
{
    int i;
    if (flat != n) MM_RAISE(MM_SIZEMM);
    for (i = 0; i < n; i++) dst[i * dstep] = src[i * sstep];
}

MMG_FN void mm_arr_copy_s(char (*dst)[MM_STRSZ], int dstep,
                          char (*src)[MM_STRSZ], int sstep, int n, int flat)
{
    int i;
    if (flat != n) MM_RAISE(MM_SIZEMM);
    for (i = 0; i < n; i++) mm_sset(dst[i * dstep], src[i * sstep]);
}

/* ---- MATH() array reductions ---------------------------------------
 * SD is the sample form, sqrt(var / (n - 1)), matching the firmware.
 * MEDIAN uses selection rather than a sorted copy so that nothing has
 * to be allocated and the caller's array is never disturbed. */

MMG_FN MMFLOAT mm_st_sum_i(const MMINTEGER *a, int n)
{ int i; MMFLOAT s = 0; for (i = 0; i < n; i++) s += (MMFLOAT)a[i]; return s; }

MMG_FN MMFLOAT mm_st_sum_f(const MMFLOAT *a, int n)
{ int i; MMFLOAT s = 0; for (i = 0; i < n; i++) s += (MMFLOAT)a[i]; return s; }

MMG_FN MMFLOAT mm_st_mean_i(const MMINTEGER *a, int n)
{ return n ? mm_st_sum_i(a, n) / n : 0; }

MMG_FN MMFLOAT mm_st_mean_f(const MMFLOAT *a, int n)
{ return n ? mm_st_sum_f(a, n) / n : 0; }

MMG_FN MMFLOAT mm_st_sd_i(const MMINTEGER *a, int n)
{
    int i; MMFLOAT m, var = 0, d;
    if (n < 2) return 0;
    m = mm_st_mean_i(a, n);
    for (i = 0; i < n; i++) { d = (MMFLOAT)a[i] - m; var += d * d; }
    return sqrt(var / (n - 1));
}

MMG_FN MMFLOAT mm_st_sd_f(const MMFLOAT *a, int n)
{
    int i; MMFLOAT m, var = 0, d;
    if (n < 2) return 0;
    m = mm_st_mean_f(a, n);
    for (i = 0; i < n; i++) { d = (MMFLOAT)a[i] - m; var += d * d; }
    return sqrt(var / (n - 1));
}

MMG_FN MMFLOAT mm_st_max_i(const MMINTEGER *a, int n, MMINTEGER *idx)
{
    int i, k = 0;
    if (n <= 0) return 0;
    for (i = 1; i < n; i++) if (a[i] > a[k]) k = i;
    if (idx) *idx = k;
    return (MMFLOAT)a[k];
}

MMG_FN MMFLOAT mm_st_max_f(const MMFLOAT *a, int n, MMINTEGER *idx)
{
    int i, k = 0;
    if (n <= 0) return 0;
    for (i = 1; i < n; i++) if (a[i] > a[k]) k = i;
    if (idx) *idx = k;
    return (MMFLOAT)a[k];
}

MMG_FN MMFLOAT mm_st_min_i(const MMINTEGER *a, int n, MMINTEGER *idx)
{
    int i, k = 0;
    if (n <= 0) return 0;
    for (i = 1; i < n; i++) if (a[i] < a[k]) k = i;
    if (idx) *idx = k;
    return (MMFLOAT)a[k];
}

MMG_FN MMFLOAT mm_st_min_f(const MMFLOAT *a, int n, MMINTEGER *idx)
{
    int i, k = 0;
    if (n <= 0) return 0;
    for (i = 1; i < n; i++) if (a[i] < a[k]) k = i;
    if (idx) *idx = k;
    return (MMFLOAT)a[k];
}

MMG_FN MMFLOAT mm_kth_i(const MMINTEGER *a, int n, int k)
{
    int i, j, less, eq;
    for (i = 0; i < n; i++) {
        less = eq = 0;
        for (j = 0; j < n; j++) {
            if (a[j] < a[i]) less++;
            else if (a[j] == a[i]) eq++;
        }
        if (less <= k && k < less + eq) return (MMFLOAT)a[i];
    }
    return 0;
}

MMG_FN MMFLOAT mm_kth_f(const MMFLOAT *a, int n, int k)
{
    int i, j, less, eq;
    for (i = 0; i < n; i++) {
        less = eq = 0;
        for (j = 0; j < n; j++) {
            if (a[j] < a[i]) less++;
            else if (a[j] == a[i]) eq++;
        }
        if (less <= k && k < less + eq) return (MMFLOAT)a[i];
    }
    return 0;
}

MMG_FN MMFLOAT mm_st_med_i(const MMINTEGER *a, int n)
{
    if (n <= 0) return 0;
    if (n & 1) return mm_kth_i(a, n, n / 2);
    return (mm_kth_i(a, n, n / 2 - 1) + mm_kth_i(a, n, n / 2)) / 2.0;
}

MMG_FN MMFLOAT mm_st_med_f(const MMFLOAT *a, int n)
{
    if (n <= 0) return 0;
    if (n & 1) return mm_kth_f(a, n, n / 2);
    return (mm_kth_f(a, n, n / 2 - 1) + mm_kth_f(a, n, n / 2)) / 2.0;
}

#endif /* MMB_ARRAY_H */

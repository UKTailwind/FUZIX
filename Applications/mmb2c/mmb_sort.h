#ifndef MMB_SORT_H
#define MMB_SORT_H
/*
 *	SORT array() [, index()] [, flags] [, start] [, count]
 *
 *	Shell sort, in place, moving the optional index array in step -
 *	no scratch memory, no recursion, both of which matter on a Pico.
 *	flags: bit0 reverse, bit1 case independent, bit2 empty strings
 *	last, exactly as cmd_sort has them.
 *
 *	WHY A HEADER rather than the runtime - the mmb_math.h bargain.
 *	As runtime entry points these were three wrappers and three rows
 *	of bcrun's lookup table plus 1.2K of engine, carried by every
 *	program on the machine whether or not it ever sorts; bcrun is the
 *	floor under what a BASIC program has left to work in.  Here the
 *	code is compiled into the program that asked for it, exactly as
 *	the graphics and pin headers are, and costs everything else
 *	nothing.  Moved out of mmb_runtime.c on 2026-08-21, byte for byte
 *	except that the two shell-sort macro instantiations are written
 *	out as functions (the board cpp is the least-proven pass on the
 *	machine and a header must not lean on it) and the case fold is
 *	ASCII arithmetic rather than ctype's - the same fold in every
 *	locale, which is also what the firmware's own str_inc does.
 *
 *	mm_sset and mm_scmp stay in the runtime: every string in the
 *	program goes through them, so they are already paid for.
 */

#ifndef MMG_FN
#if defined(MM_FCC) || defined(MM_PC3)
#define MMG_FN static
#else
#define MMG_FN static __inline__ __attribute__((unused))
#endif
#endif

MMG_FN int mm_scmp_ci(const char *a, const char *b)
{
    int la = mm_slen(a), lb = mm_slen(b), n = la < lb ? la : lb, i, ca, cb;
    for (i = 1; i <= n; i++) {
        ca = (unsigned char)a[i];
        cb = (unsigned char)b[i];
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    if (la == lb) return 0;
    return la < lb ? -1 : 1;
}

MMG_FN int mm_sort_cmp_s(const char *a, const char *b, int flags)
{
    int r;
    if (flags & 4) {                       /* empty strings to the end */
        int ea = mm_slen(a) == 0, eb = mm_slen(b) == 0;
        if (ea != eb) return ea ? 1 : -1;
    }
    r = (flags & 2) ? mm_scmp_ci(a, b) : mm_scmp(a, b);
    return (flags & 1) ? -r : r;
}

MMG_FN void mm_sort_range(int *start, int *count, int total)
{
    if (*start < 0) *start = 0;
    if (*start > total) *start = total;
    if (*count < 0 || *start + *count > total) *count = total - *start;
}

MMG_FN void mm_seed_index(MMINTEGER *idx, int total)
{
    int i;
    if (!idx) return;
    for (i = 0; i < total; i++) idx[i] = i;
}

MMG_FN void mm_sort_i(MMINTEGER *a, MMINTEGER *idx, int total, int start,
                      int count, int flags)
{
    int gap, i, j;

    mm_sort_range(&start, &count, total);
    mm_seed_index(idx, total);
    for (gap = count / 2; gap > 0; gap /= 2)
        for (i = gap; i < count; i++) {
            MMINTEGER tv = a[start + i];
            MMINTEGER ti = idx ? idx[start + i] : 0;
            for (j = i;
                 j >= gap && ((flags & 1) ? a[start + j - gap] < tv
                                          : a[start + j - gap] > tv);
                 j -= gap) {
                a[start + j] = a[start + j - gap];
                if (idx) idx[start + j] = idx[start + j - gap];
            }
            a[start + j] = tv;
            if (idx) idx[start + j] = ti;
        }
}

MMG_FN void mm_sort_f(MMFLOAT *a, MMINTEGER *idx, int total, int start,
                      int count, int flags)
{
    int gap, i, j;

    mm_sort_range(&start, &count, total);
    mm_seed_index(idx, total);
    for (gap = count / 2; gap > 0; gap /= 2)
        for (i = gap; i < count; i++) {
            MMFLOAT tv = a[start + i];
            MMINTEGER ti = idx ? idx[start + i] : 0;
            for (j = i;
                 j >= gap && ((flags & 1) ? a[start + j - gap] < tv
                                          : a[start + j - gap] > tv);
                 j -= gap) {
                a[start + j] = a[start + j - gap];
                if (idx) idx[start + j] = idx[start + j - gap];
            }
            a[start + j] = tv;
            if (idx) idx[start + j] = ti;
        }
}

MMG_FN void mm_sort_s(char (*a)[MM_STRSZ], MMINTEGER *idx, int total,
                      int start, int count, int flags)
{
    int gap, i, j;
    char tv[MM_STRSZ];

    mm_sort_range(&start, &count, total);
    mm_seed_index(idx, total);
    for (gap = count / 2; gap > 0; gap /= 2)
        for (i = gap; i < count; i++) {
            MMINTEGER ti = idx ? idx[start + i] : 0;
            mm_sset(tv, a[start + i]);
            for (j = i;
                 j >= gap && mm_sort_cmp_s(a[start + j - gap], tv, flags) > 0;
                 j -= gap) {
                mm_sset(a[start + j], a[start + j - gap]);
                if (idx) idx[start + j] = idx[start + j - gap];
            }
            mm_sset(a[start + j], tv);
            if (idx) idx[start + j] = ti;
        }
}

#endif /* MMB_SORT_H */

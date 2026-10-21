#ifndef MMB_LSTRING_H
#define MMB_LSTRING_H
/*
 *	LONGSTRING - byte count in a[0], payload from (char *)&a[1]
 *	onward, not NUL terminated; the firmware's convention (see
 *	misc/Custom.c).  cells is the array's total element count, so
 *	the payload capacity is (cells - 1) * 8 bytes, and every entry
 *	point refuses to run past the end - the manual calls an overflow
 *	"undefined", so a clean error is a strict improvement.
 *
 *	WHY A HEADER rather than the runtime - the mmb_math.h bargain:
 *	these were 21 wrappers and rows in bcrun's lookup table over
 *	1.9K of memcpy arithmetic, carried by every program on the
 *	machine whether or not it ever says LONGSTRING.  Moved out of
 *	mmb_runtime.c on 2026-08-21, byte for byte except that the case
 *	folds are ASCII arithmetic rather than ctype's - the same fold
 *	in every locale.
 *
 *	mm_ls_print and mm_ls_input STAY in the runtime: a file channel
 *	is bcrun's own stdio stream, which only bcrun can hand to fputc
 *	and fread, so those two keep their crossing (and mm_ls_file with
 *	them).
 */

#include <string.h>

#ifndef MMG_FN
#if defined(MM_FCC) || defined(MM_PC3)
#define MMG_FN static
#else
#define MMG_FN static __inline__ __attribute__((unused))
#endif
#endif

#define MM_LS_DATA(a)  ((char *)&(a)[1])
#define MM_LS_CAP(c)   (((c) - 1) * (int)sizeof(MMINTEGER))

MMG_FN MMINTEGER mm_ls_len(const MMINTEGER *a) { return a[0] < 0 ? 0 : a[0]; }

/* The capacity check, with the message the reference gives for that
 * operation (MM_Misc.c): APPEND and LOAD "Integer array too small"; LEFT,
 * RIGHT, MID, COPY and CONCAT StandardError(23); LMID "String too long".
 * Returns 1 when it raised and the caller returns - under ON ERROR SKIP
 * carrying on would have copied past the end of the array. */
#define MM_LS_SMALL_INT  "Integer array too small"
#define MM_LS_SMALL_DEST "Destination array too small"

MMG_FN int mm_ls_fit(int cells, MMINTEGER want, const char *msg)
{
    if (want < 0 || want > MM_LS_CAP(cells)) {
        mm_error(msg);
        return 1;
    }
    return 0;
}

MMG_FN void mm_ls_clear(MMINTEGER *a, int cells) { (void)cells; a[0] = 0; }

MMG_FN void mm_ls_append(MMINTEGER *a, int cells, const char *s)
{
    MMINTEGER n = mm_ls_len(a), k = mm_slen(s);
    if (mm_ls_fit(cells, n + k, MM_LS_SMALL_INT)) return;
    memcpy(MM_LS_DATA(a) + n, s + 1, (size_t)k);
    a[0] = n + k;
}

/* The counts are the reference's getint(…, 0, 0x7FFFFFFF): a negative one
   is its range error (8d44510, issue #72), where it used to be taken as 0. */
MMG_FN void mm_ls_load(MMINTEGER *a, int cells, MMINTEGER n, const char *s)
{
    MMINTEGER k = mm_slen(s);
    if (mm_range(n, 0, 0x7FFFFFFF)) return;
    if (n > k) n = k;
    if (mm_ls_fit(cells, n, MM_LS_SMALL_INT)) return;
    memcpy(MM_LS_DATA(a), s + 1, (size_t)n);
    a[0] = n;
}

MMG_FN void mm_ls_copy(MMINTEGER *d, int dcells, const MMINTEGER *s)
{
    MMINTEGER n = mm_ls_len(s);
    if (mm_ls_fit(dcells, n, MM_LS_SMALL_DEST)) return;
    memmove(MM_LS_DATA(d), MM_LS_DATA((MMINTEGER *)s), (size_t)n);
    d[0] = n;
}

MMG_FN void mm_ls_concat(MMINTEGER *d, int dcells, const MMINTEGER *s)
{
    MMINTEGER n = mm_ls_len(d), k = mm_ls_len(s);
    if (mm_ls_fit(dcells, n + k, MM_LS_SMALL_DEST)) return;
    memmove(MM_LS_DATA(d) + n, MM_LS_DATA((MMINTEGER *)s), (size_t)k);
    d[0] = n + k;
}

MMG_FN void mm_ls_left(MMINTEGER *d, int dcells, const MMINTEGER *s,
                       MMINTEGER n)
{
    MMINTEGER k = mm_ls_len(s);
    if (mm_range(n, 0, 0x7FFFFFFF)) return;
    if (n > k) n = k;
    if (mm_ls_fit(dcells, n, MM_LS_SMALL_DEST)) return;
    memmove(MM_LS_DATA(d), MM_LS_DATA((MMINTEGER *)s), (size_t)n);
    d[0] = n;
}

MMG_FN void mm_ls_right(MMINTEGER *d, int dcells, const MMINTEGER *s,
                        MMINTEGER n)
{
    MMINTEGER k = mm_ls_len(s);
    if (mm_range(n, 0, 0x7FFFFFFF)) return;
    if (n > k) n = k;
    if (mm_ls_fit(dcells, n, MM_LS_SMALL_DEST)) return;
    memmove(MM_LS_DATA(d), MM_LS_DATA((MMINTEGER *)s) + (k - n), (size_t)n);
    d[0] = n;
}

/* MID's start is the reference's getint(…, 1, length) - outside it is an
   error, not a clamp.  n < 0 is "count omitted", the translator's -1, so
   an explicit negative count is not yet told from a missing one. */
MMG_FN void mm_ls_mid(MMINTEGER *d, int dcells, const MMINTEGER *s,
                      MMINTEGER start, MMINTEGER n)
{
    MMINTEGER k = mm_ls_len(s), avail;
    if (mm_range(start, 1, k)) return;
    if (n >= 0 && mm_range(n, 0, 0x7FFFFFFF)) return;
    avail = k - start + 1;
    if (n < 0 || n > avail) n = avail;
    if (mm_ls_fit(dcells, n, MM_LS_SMALL_DEST)) return;
    memmove(MM_LS_DATA(d), MM_LS_DATA((MMINTEGER *)s) + (start - 1),
            (size_t)n);
    d[0] = n;
}

/* REPLACE writes over what is there and never grows the string: its
   position is getint(…, 1, length - len(s$) + 1).  It used to clamp a
   position below 1 and extend the string past its end. */
MMG_FN void mm_ls_replace(MMINTEGER *a, int cells, const char *s,
                          MMINTEGER start)
{
    MMINTEGER k = mm_slen(s);
    (void)cells;
    if (mm_range(start, 1, mm_ls_len(a) - k + 1)) return;
    memcpy(MM_LS_DATA(a) + (start - 1), s + 1, (size_t)k);
}

/*
 * LMID(a(), start [, num]) = s$ - a SPLICE, not an overwrite: num
 * bytes at start come out and the string goes in, so the long string
 * changes length.  num < 0 means "as long as the replacement".  The
 * bound is one tighter than cmd_lmid's, deliberately: MMBasic's own
 * test is off by one and lets a selection run one byte past the end,
 * and erring on a slice that does not exist beats a memmove of
 * (size_t)-1.  No program that stays inside its string can tell.
 */
MMG_FN void mm_ls_lmid(MMINTEGER *a, int cells, MMINTEGER start,
                       MMINTEGER num, const char *s)
{
    MMINTEGER cur = mm_ls_len(a);
    MMINTEGER rl = mm_slen(s);
    MMINTEGER change;

    /* the reference's getints (cmd_lmid): start 1..length, num 0..length */
    if (mm_range(start, 1, cur))
        return;
    if (num < 0)
        num = rl;               /* omitted: as long as the replacement */
    else if (mm_range(num, 0, cur))
        return;
    if (start + num - 1 > cur) {
        mm_error("Selection exceeds length of string");
        return;
    }
    start--;                    /* position 1 is offset 0 */
    change = rl - num;
    if (change == 0) {
        memcpy(MM_LS_DATA(a) + start, s + 1, (size_t)rl);
        return;
    }
    if (mm_ls_fit(cells, cur + change, "String too long"))
        return;
    /*	Move the tail before writing, and move it with memmove: the two
     *	regions overlap whenever the replacement is shorter. */
    memmove(MM_LS_DATA(a) + start + rl, MM_LS_DATA(a) + start + num,
            (size_t)(cur - start - num));
    memcpy(MM_LS_DATA(a) + start, s + 1, (size_t)rl);
    a[0] = cur + change;
}

/* RESIZE: the reference's getint(…, 0, capacity) */
MMG_FN void mm_ls_resize(MMINTEGER *a, int cells, MMINTEGER n)
{
    if (mm_range(n, 0, MM_LS_CAP(cells)))
        return;
    a[0] = n;
}

MMG_FN void mm_ls_setbyte(MMINTEGER *a, int cells, MMINTEGER n, MMINTEGER v)
{
    /* SETBYTE and LGETBYTE respect OPTION BASE; the caller has already
       folded the base in, so n is 0 based here.  The value is the
       reference's getint(…, 0, 255), not masked. */
    if (mm_ls_fit(cells, n + 1, "Long string is too small for this operation")
        || mm_range(v, 0, 255))
        return;
    MM_LS_DATA(a)[n] = (char)(unsigned char)v;
    if (n + 1 > mm_ls_len(a)) a[0] = n + 1;
}

MMG_FN void mm_ls_trim(MMINTEGER *a, int cells, MMINTEGER n)
{
    MMINTEGER k = mm_ls_len(a);
    (void)cells;
    if (n < 0) n = 0;
    if (n > k) n = k;
    memmove(MM_LS_DATA(a), MM_LS_DATA(a) + n, (size_t)(k - n));
    a[0] = k - n;
}

MMG_FN void mm_ls_ucase(MMINTEGER *a)
{
    MMINTEGER i, n = mm_ls_len(a);
    char *p = MM_LS_DATA(a);
    for (i = 0; i < n; i++)
        if (p[i] >= 'a' && p[i] <= 'z') p[i] = (char)(p[i] - 32);
}

MMG_FN void mm_ls_lcase(MMINTEGER *a)
{
    MMINTEGER i, n = mm_ls_len(a);
    char *p = MM_LS_DATA(a);
    for (i = 0; i < n; i++)
        if (p[i] >= 'A' && p[i] <= 'Z') p[i] = (char)(p[i] + 32);
}

MMG_FN char *mm_ls_getstr(const MMINTEGER *a, MMINTEGER start, MMINTEGER len)
{
    char *t = mm_tmp();
    MMINTEGER n = mm_ls_len(a), avail;
    if (start < 1) start = 1;
    if (start > n) return t;
    avail = n - start + 1;
    if (len < 0 || len > avail) len = avail;
    /* t, not mm_ssink(): the sink is a runtime static with no libcall
       row, and the scratch temp already in hand is exactly the
       "writable empty string" a poisoned caller needs. */
    if (len > MM_STRLEN)
        MM_RAISEV("LGETSTR$ result is longer than 255 characters", t);
    mm_ssetn(t, MM_LS_DATA((MMINTEGER *)a) + (start - 1), (int)len);
    return t;
}

MMG_FN MMINTEGER mm_ls_getbyte(const MMINTEGER *a, MMINTEGER n, int base)
{
    MMINTEGER k = n - base;              /* fold OPTION BASE into 0 based */
    if (k < 0 || k >= mm_ls_len(a)) mm_error("LGETBYTE index out of range");
    return (MMINTEGER)(unsigned char)MM_LS_DATA((MMINTEGER *)a)[k];
}

MMG_FN MMINTEGER mm_ls_instr(const MMINTEGER *a, const char *pat,
                             MMINTEGER start)
{
    MMINTEGER n = mm_ls_len(a), i;
    int lp = mm_slen(pat);
    const char *p = MM_LS_DATA((MMINTEGER *)a);
    /* fun_LInstr: a start below 1 finds nothing - it used to search from 1 */
    if (start < 1 || lp == 0 || start > n - lp + 1) return 0;
    for (i = start; i + lp - 1 <= n; i++)
        if (memcmp(p + i - 1, pat + 1, (size_t)lp) == 0) return i;
    return 0;
}

MMG_FN MMINTEGER mm_ls_compare(const MMINTEGER *a, const MMINTEGER *b)
{
    MMINTEGER la = mm_ls_len(a), lb = mm_ls_len(b);
    MMINTEGER n = la < lb ? la : lb;
    int r = n ? memcmp(MM_LS_DATA((MMINTEGER *)a),
                       MM_LS_DATA((MMINTEGER *)b), (size_t)n) : 0;
    if (r) return r < 0 ? -1 : 1;
    if (la == lb) return 0;
    return la < lb ? -1 : 1;
}

#endif /* MMB_LSTRING_H */

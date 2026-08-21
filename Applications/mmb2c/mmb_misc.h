#ifndef MMB_MISC_H
#define MMB_MISC_H
/*
 *	The small pure families, together because each is a few dozen
 *	bytes and none is recursive, so cc1's dead-static rule drops
 *	whatever a program does not name:
 *
 *	    GOSUB / RETURN          the site stack
 *	    BIT() = / BYTE() =      assignments that reach into a variable
 *	    FLAG() / FLAGS          64 bits of program scratch
 *	    BYTE()                  the reader
 *	    BIN2STR$ / STR2BIN      type punning to and from strings
 *	    TRIM$ / FIELD$          mask trimming and field splitting
 *	    MAP() / COLOUR MAP      the fixed RGB121 palette arithmetic
 *
 *	WHY A HEADER rather than the runtime - the mmb_math.h bargain:
 *	every one of these is computation over the program's own values,
 *	and as runtime entry points they were 17 wrappers and rows in
 *	bcrun's lookup table carried by every program on the machine.
 *	Moved out of mmb_runtime.c on 2026-08-21, byte for byte, except
 *	that BIN2STR$'s overflow raise returns the scratch temp already
 *	in hand rather than mm_ssink() - the sink is a runtime static
 *	with no libcall row (the mm_ls_getstr lesson).
 */

#include <string.h>

#ifndef MMG_FN
#if defined(MM_FCC) || defined(MM_PC3)
#define MMG_FN static
#else
#define MMG_FN static __inline__ __attribute__((unused))
#endif
#endif

/* ---- GOSUB / RETURN --------------------------------------------------
 * A GOSUB compiles to "push a site id, goto the label" and a RETURN to
 * a switch on the popped id that jumps back.  MMBasic allows 50. */

static int mm_gstack[MM_MAXGOSUB];
static int mm_gsp;

MMG_FN void mm_gosub_push(int site)
{
    if (mm_gsp >= MM_MAXGOSUB) MM_RAISE("Too many nested GOSUB");
    mm_gstack[mm_gsp++] = site;
}

MMG_FN int mm_gosub_pop(void)
{
    if (mm_gsp <= 0) mm_error("RETURN without GOSUB");
    return mm_gstack[--mm_gsp];
}

/* ---- BIT() = / BYTE() = / FLAG --------------------------------------
 * MMBasic's cmd_bit and cmd_byte: assignments that reach INTO a
 * variable.  The type check is the translator's; the range checks
 * raise rather than clamp because MMBasic's getint does. */

MMG_FN void mm_bit_assign(MMINTEGER *p, MMINTEGER n, MMINTEGER v)
{
    if (n < 0 || n > 63) {
        mm_error("Bit number is out of bounds");
        return;
    }
    if (v < 0 || v > 1) {
        mm_error("Bit value must be 0 or 1");
        return;
    }
    if (v)
        *p |= (MMINTEGER)1 << n;
    else
        *p &= ~((MMINTEGER)1 << n);
}

MMG_FN void mm_byte_assign(char *s, MMINTEGER n, MMINTEGER v)
{
    /*	One based, and the upper bound is the string's CURRENT length -
     *	BYTE writes a character that is already there and never extends
     *	the string. */
    if (n < 1 || n > (MMINTEGER)mm_slen(s)) {
        mm_error("Byte position is out of bounds");
        return;
    }
    if (v < 0 || v > 255) {
        mm_error("Byte value must be 0 to 255");
        return;
    }
    s[n] = (char)(unsigned char)v;
}

/* Sixty-four bits of nothing in particular - MMBasic's g_flag, cleared
 * when a program starts.  A static here is per process, which the
 * firmware's single global could not be. */
static MMINTEGER mm_flags_v;

MMG_FN void mm_flag_assign(MMINTEGER n, MMINTEGER v)
{
    if (n < 0 || n > 63) {
        mm_error("Flag number is out of bounds");
        return;
    }
    if (v < 0 || v > 1) {
        mm_error("Flag value must be 0 or 1");
        return;
    }
    if (v)
        mm_flags_v |= (MMINTEGER)1 << n;
    else
        mm_flags_v &= ~((MMINTEGER)1 << n);
}

MMG_FN void mm_flags_set(MMINTEGER v) { mm_flags_v = v; }

MMG_FN MMINTEGER mm_flag_get(MMINTEGER n)
{
    if (n < 0 || n > 63) {
        mm_error("Flag number is out of bounds");
        return 0;
    }
    return (mm_flags_v >> n) & 1;
}

MMG_FN MMINTEGER mm_flags_get(void) { return mm_flags_v; }

MMG_FN MMINTEGER mm_byte(const char *s, MMINTEGER n)
{
    if (n < 1 || n > mm_slen(s))
        MM_RAISEV("Index out of bounds in BYTE()", 0);
    return (MMINTEGER)(unsigned char)s[n];
}

/* ---- BIN2STR$ / STR2BIN --------------------------------------------- */

MMG_FN char *mm_bin2str(int type, MMFLOAT fv, MMINTEGER iv, int big)
{
    unsigned char raw[8];
    int n, i;
    char *t = mm_tmp();
    float f32;
    double f64;
    switch (type) {
    case MM_B_INT64: case MM_B_UINT64: n = 8; break;
    case MM_B_INT32: case MM_B_UINT32: n = 4; break;
    case MM_B_INT16: case MM_B_UINT16: n = 2; break;
    case MM_B_INT8:  case MM_B_UINT8:  n = 1; break;
    case MM_B_SINGLE: n = 4; break;
    default:          n = 8; break;
    }
    if (type == MM_B_SINGLE)      { f32 = (float)fv;  memcpy(raw, &f32, 4); }
    else if (type == MM_B_DOUBLE) { f64 = (double)fv; memcpy(raw, &f64, 8); }
    else {
        unsigned long long u = (unsigned long long)iv;
        long long lo = 0, hi = 0;
        switch (type) {
        case MM_B_INT32:  lo = -2147483648LL; hi = 2147483647LL; break;
        case MM_B_UINT32: lo = 0;             hi = 4294967295LL; break;
        case MM_B_INT16:  lo = -32768;        hi = 32767;        break;
        case MM_B_UINT16: lo = 0;             hi = 65535;        break;
        case MM_B_INT8:   lo = -128;          hi = 127;          break;
        case MM_B_UINT8:  lo = 0;             hi = 255;          break;
        default:          lo = 0;             hi = 0;            break;
        }
        if (hi != 0 && (iv < lo || iv > hi))
            MM_RAISEV("Overflow", t);
        for (i = 0; i < n; i++) raw[i] = (unsigned char)((u >> (8 * i)) & 0xFF);
    }
    if (big) { for (i = 0; i < n / 2; i++)
                   { unsigned char c = raw[i]; raw[i] = raw[n-1-i]; raw[n-1-i] = c; } }
    mm_ssetn(t, (const char *)raw, n);
    return t;
}

MMG_FN MMFLOAT mm_str2bin_f(int type, const char *s, int big)
{
    unsigned char raw[8];
    int n = (type == MM_B_SINGLE) ? 4 : 8, i;
    float f32; double f64;
    if (mm_slen(s) != n) MM_RAISEV("String length", 0.0);
    memcpy(raw, s + 1, (size_t)n);
    if (big) { for (i = 0; i < n / 2; i++)
                   { unsigned char c = raw[i]; raw[i] = raw[n-1-i]; raw[n-1-i] = c; } }
    if (type == MM_B_SINGLE) { memcpy(&f32, raw, 4); return (MMFLOAT)f32; }
    memcpy(&f64, raw, 8); return (MMFLOAT)f64;
}

MMG_FN MMINTEGER mm_str2bin_i(int type, const char *s, int big)
{
    unsigned char raw[8];
    unsigned long long u = 0;
    int n, i;
    switch (type) {
    case MM_B_INT64: case MM_B_UINT64: n = 8; break;
    case MM_B_INT32: case MM_B_UINT32: n = 4; break;
    case MM_B_INT16: case MM_B_UINT16: n = 2; break;
    default:                           n = 1; break;
    }
    if (mm_slen(s) != n) MM_RAISEV("String length", 0);
    memcpy(raw, s + 1, (size_t)n);
    if (big) { for (i = 0; i < n / 2; i++)
                   { unsigned char c = raw[i]; raw[i] = raw[n-1-i]; raw[n-1-i] = c; } }
    for (i = n - 1; i >= 0; i--) u = (u << 8) | raw[i];
    switch (type) {                      /* sign extend the signed forms */
    case MM_B_INT32: return (MMINTEGER)(int32_t)u;
    case MM_B_INT16: return (MMINTEGER)(int16_t)u;
    case MM_B_INT8:  return (MMINTEGER)(int8_t)u;
    default:         return (MMINTEGER)(int64_t)u;
    }
}

/* ---- TRIM$ / FIELD$ ------------------------------------------------- */

MMG_FN int mm_in_mask(char c, const char *mask)
{
    int n = mm_slen(mask), i;
    for (i = 1; i <= n; i++) if (mask[i] == c) return 1;
    return 0;
}

MMG_FN char *mm_trim(const char *src, const char *mask, int where)
{
    int len = mm_slen(src), start = 1, end = len;
    char *t = mm_tmp();
    if (where == 'L' || where == 'B')
        while (start <= end && mm_in_mask(src[start], mask)) start++;
    if (where == 'R' || where == 'B')
        while (end >= start && mm_in_mask(src[end], mask)) end--;
    mm_ssetn(t, src + start, end - start + 1);
    return t;
}

/* mirrors scan_for_delimiter() in core/Functions.c */
MMG_FN int mm_scan_delim(int start, const char *p, const char *delims,
                         const char *quotes)
{
    int i, n = mm_slen(p);
    char qidx;
    for (i = start; i <= n && !mm_in_mask(p[i], delims); i++) {
        if (mm_in_mask(p[i], quotes)) {
            qidx = p[i];
            i++;
            while (i < n && p[i] != qidx) i++;
        }
    }
    return i;
}

MMG_FN char *mm_field(const char *p, MMINTEGER fnbr, const char *delims,
                      const char *quotes)
{
    char *t = mm_tmp();
    int i = 1, j, k, n = mm_slen(p);
    while (--fnbr > 0) {
        i = mm_scan_delim(i, p, delims, quotes);
        if (i > n) return t;
        i++;
    }
    while (i <= n && p[i] == ' ') i++;
    j = mm_scan_delim(i, p, delims, quotes);
    k = j - i;
    if (k < 0) k = 0;
    mm_ssetn(t, p + i, k);
    k = mm_slen(t);
    while (k > 0 && t[k] == ' ') k--;
    t[0] = (char)(unsigned char)k;
    t[k + 1] = 0;
    return t;
}

/* ---- MAP() / COLOUR MAP ---------------------------------------------
 * The DEFAULT palette arithmetic - MMBasic's fun_map, the inverse of
 * RGB121(), deliberately unaffected by any remapping.  MAP the
 * statement (the live palette) stays a kernel crossing. */

MMG_FN MMINTEGER mm_map_get(MMINTEGER index)
{
    MMINTEGER n = index & 15;

    return ((n & 8) << 20) | ((n & 6) << 13) | ((n & 1) << 7);
}

MMG_FN void mm_colour_map(const MMINTEGER *in, int n, MMINTEGER *out,
                          int outn, const MMINTEGER *map, int mapn)
{
    int i;

    if (outn != n)
        MM_RAISE("Array size mismatch");
    if (map) {
        if (mapn != 16)
            MM_RAISE("Array size not 16 elements");
        for (i = 0; i < 16; i++)
            if (map[i] < 0 || map[i] > 0xFFFFFF)
                MM_RAISE("Invalid colour");
    }
    for (i = 0; i < n; i++) {
        MMINTEGER c = in[i];
        if (c < 0 || c > 15)
            MM_RAISE("Input range error on element");
        out[i] = map ? map[c] : mm_map_get(c);
    }
}

#endif /* MMB_MISC_H */

/* mmbc_font.c - DefineFont ... End DefineFont.
 *
 * Mirrors mmb2c.py pass_fonts / define_font, and must agree with it to
 * the byte: cgate.sh diffs the two translators' output.
 *
 * Why a pass of its own, before every other one: MMBasic binds fonts
 * when the program is LOADED (MMBasic.c walks the CFunction area
 * filling FontTable[]), so a block at the bottom of a file is in force
 * at the top of it - picofrog selects its font at line 95 and defines
 * it at line 1324.  And the body is hex, not BASIC, so it is taken at
 * the LINE level and the lines are blanked afterwards; nothing
 * downstream should try to read `5F200808' as an expression.
 *
 * Each 8-digit group is a 32-bit LITTLE-ENDIAN word, so the bytes come
 * out reversed - and are then exactly the layout the kernel's own
 * fonts use (width, height, first character, count, then the glyphs
 * MSB first).  That is the whole trick: no bit reordering anywhere,
 * one swap here at translation time.
 *
 * No <ctype.h>: nothing else in mmbc uses it (the board's cc has its
 * own include set), so the character tests are written out.
 */

#include "mmbc.h"

static int hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* A scratch copy with the whitespace off both ends. */
static char *fstrip(const char *s)
{
    const char *e;
    char *out;
    size_t n;

    while (*s && is_space(*s))
        s++;
    e = s + strlen(s);
    while (e > s && is_space(e[-1]))
        e--;
    n = (size_t)(e - s);
    out = salloc(n + 1);
    memcpy(out, s, n);
    out[n] = 0;
    return out;
}

/* One collected block: check it and keep the bytes. */
static void define_font(int fno, const unsigned long *words, int nwords,
                        int where)
{
    unsigned char *data;
    int wid, hgt, count, need, k, n;

    /* 1-9 are the built-in nine, shared with the console and every
     * other program, so they cannot be replaced - and saying so is the
     * point.  A silently ignored DefineFont would draw in the wrong
     * glyphs and look like a rendering bug. */
    if (fno < 10 || fno > 16) {
        errors_add(sfmt("line %d: DefineFont %d - user fonts are 10 to 16 "
                        "(1-9 are built in)", where, fno));
        return;
    }
    for (k = 0; k < cv.nfonts; k++)
        if (cv.fonts[k].num == fno) {
            errors_add(sfmt("line %d: font %d is defined twice",
                            where, fno));
            return;
        }
    if (nwords == 0) {
        errors_add(sfmt("line %d: DefineFont %d is empty", where, fno));
        return;
    }
    n = nwords * 4;
    data = palloc((size_t)n);
    for (k = 0; k < nwords; k++) {
        data[k * 4 + 0] = (unsigned char)(words[k] & 0xFF);
        data[k * 4 + 1] = (unsigned char)((words[k] >> 8) & 0xFF);
        data[k * 4 + 2] = (unsigned char)((words[k] >> 16) & 0xFF);
        data[k * 4 + 3] = (unsigned char)((words[k] >> 24) & 0xFF);
    }
    wid = data[0];
    hgt = data[1];
    count = data[3];
    if (wid == 0 || hgt == 0 || count == 0) {
        errors_add(sfmt("line %d: font %d has a zero in its header "
                        "(width %d, height %d, count %d)",
                        where, fno, wid, hgt, count));
        return;
    }
    /* what makes the glyphs plain MSB-first bytes rather than a bit
     * stream - the renderer assumes it, as MMBasic's does */
    if ((wid * hgt) % 8) {
        errors_add(sfmt("line %d: font %d is %dx%d - width times height "
                        "must be a multiple of 8", where, fno, wid, hgt));
        return;
    }
    need = 4 + count * (wid * hgt / 8);
    if (n < need) {
        errors_add(sfmt("line %d: font %d says %d characters of %dx%d "
                        "(%d bytes) but carries %d",
                        where, fno, count, wid, hgt, need, n));
        return;
    }
    /* trailing padding a tracker may have left is dropped: the header
     * is the authority on where the font ends */
    GROW(cv.fonts, cv.nfonts, cv.cfonts);
    cv.fonts[cv.nfonts].num = fno;
    cv.fonts[cv.nfonts].data = data;
    cv.fonts[cv.nfonts].len = need;
    cv.nfonts++;
}

/* Font-number order, so the emitter walks this the way the Python
 * walks sorted(self.fonts).  At most seven, so an insertion sort. */
static void fonts_sort(void)
{
    int i, j;

    for (i = 1; i < cv.nfonts; i++) {
        struct fontdef t = cv.fonts[i];
        for (j = i - 1; j >= 0 && cv.fonts[j].num > t.num; j--)
            cv.fonts[j + 1] = cv.fonts[j];
        cv.fonts[j + 1] = t;
    }
}

void pass_fonts(void)
{
    unsigned long *words = NULL;
    int nwords = 0, cwords = 0;
    int i = 0;

    while (i < src_nlines) {
        char *line, *up, *p, *num;
        int start, ended = 0, bad = 0, fno, skip;

        /* the scratch pool is per line here, as it is in tokenize:
         * a 1300-line program would otherwise pile up copies of every
         * line in a 256K process */
        scratch_reset();
        cv.lineno = i + 1;
        line = fstrip(src_lines[i]);
        up = upper(line);
        if (strncmp(up, "DEFINEFONT", 10) == 0)
            skip = 10;
        else if (strncmp(up, "DEFINE FONT", 11) == 0)
            skip = 11;
        else {
            i++;
            continue;
        }
        /* the number, with MMBasic's optional '#', and a trailing
         * comment allowed */
        p = line + skip;
        while (*p == ' ' || *p == '\t' || *p == '#')
            p++;
        num = p;
        while (*p && !is_space(*p) && *p != '\'' && *p != ';')
            p++;
        *p = 0;
        start = i;
        src_lines[i] = pstr("");
        i++;
        if (num[0] == 0) {
            errors_add(sfmt("line %d: DefineFont wants a font number",
                            start + 1));
            continue;
        }
        /* base 10, as the Python's int(num, 10): a leading zero must
         * not quietly become octal in one translator and not the
         * other */
        fno = (int)strtol(num, NULL, 10);
        nwords = 0;
        while (i < src_nlines) {
            char *t, *u, *q;

            t = fstrip(src_lines[i]);
            u = upper(t);
            {
                /* "End DefineFont", with or without the space */
                char packed[16];
                int j = 0, m;

                for (m = 0; u[m] && j < 15; m++)
                    if (u[m] != ' ' && u[m] != '\t')
                        packed[j++] = u[m];
                packed[j] = 0;
                src_lines[i] = pstr("");
                i++;
                if (strncmp(packed, "ENDDEFINEFONT", 13) == 0) {
                    ended = 1;
                    break;
                }
            }
            /* comments and blank lines inside the block are ignored, as
             * they are anywhere else */
            if (t[0] == '\'' || t[0] == 0)
                continue;
            if ((q = strchr(t, '\'')) != NULL)
                *q = 0;
            for (p = t; *p; ) {
                unsigned long w = 0;
                int k, v;

                while (*p && is_space(*p))
                    p++;
                if (!*p)
                    break;
                for (k = 0; k < 8 && p[k]; k++) {
                    v = hexval(p[k]);
                    if (v < 0)
                        break;
                    w = (w << 4) | (unsigned long)v;
                }
                if (k != 8 || (p[8] && !is_space(p[8]))) {
                    char bads[32];
                    int b = 0;
                    while (p[b] && !is_space(p[b]) && b < 31) {
                        bads[b] = p[b];
                        b++;
                    }
                    bads[b] = 0;
                    /* say it ONCE and keep reading to the terminator:
                     * stopping here would add a bogus "no matching End
                     * DefineFont" on top of the real complaint */
                    if (!bad)
                        errors_add(sfmt("line %d: DefineFont wants 8-digit "
                                        "hex words, not '%s'", i, bads));
                    bad = 1;
                    p += b;
                    continue;
                }
                GROW(words, nwords, cwords);
                words[nwords++] = w;
                p += 8;
            }
        }
        if (!ended) {
            errors_add(sfmt("line %d: DefineFont %d has no matching End "
                            "DefineFont", start + 1, fno));
            continue;
        }
        if (bad)
            continue;
        define_font(fno, words, nwords, start + 1);
    }
    fonts_sort();
}

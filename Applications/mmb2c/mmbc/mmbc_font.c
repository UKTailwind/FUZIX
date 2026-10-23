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

/* A line as the reference's loader compares it (FileIO.c): TABs and runs
 * of blanks as one blank, none at either end, and the letters upper
 * case - ASCII only, as the Python's is. */
char *loader_squash(const char *s)
{
    char *out = salloc(strlen(s) + 1);
    int n = 0, blank = 1;

    for (; *s; s++) {
        if (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
            if (!blank)
                out[n++] = ' ';
            blank = 1;
        } else {
            out[n++] = (*s >= 'a' && *s <= 'z') ? *s - 32 : *s;
            blank = 0;
        }
    }
    if (n > 0 && out[n - 1] == ' ')
        n--;
    out[n] = 0;
    return out;
}

/* isnamechar on an upper-cased character, 0 (the end) being none */
static int loader_namechar(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
           || c == '_' || c == '.';
}

static char ascii_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

/* Where a line's text starts: past its blanks and a line number, and the
 * blanks after that - the tokeniser takes the number off first and the
 * rest of the line is what it reads (MMBasic.c tokenise). */
static int line_start(const char *s)
{
    int i = 0;

    while (s[i] == ' ' || s[i] == '\t')
        i++;
    while (s[i] >= '0' && s[i] <= '9')
        i++;
    while (s[i] == ' ' || s[i] == '\t')
        i++;
    return i;
}

/* The first slash-star or star-slash on this line where a statement
 * starts - the line's start, after a line number, after a ':', or after
 * THEN or ELSE, where the tokeniser looks for a command - its position,
 * with *mark set to it; -1 if there is none.  Nothing in quotes, after a
 * ' or after REM is looked at.
 *
 * The manual's rule for them (FUZIX #33): they must be the first
 * non-space characters of the line and have a space or the end of the
 * line after them; pass_fonts refuses one that breaks it. */
static int block_mark_at(const char *s, const char **mark)
{
    int i = 0, start = 1, q = 0;

    while (s[i] == ' ' || s[i] == '\t')
        i++;
    while (s[i] >= '0' && s[i] <= '9')
        i++;
    for (; s[i]; i++) {
        char c = s[i];

        if (q) {
            if (c == '"')
                q = 0;
        } else if (c == ' ' || c == '\t') {
            /* blanks change nothing */
        } else if (start && ((c == '/' && s[i + 1] == '*')
                             || (c == '*' && s[i + 1] == '/'))) {
            *mark = c == '/' ? "/*" : "*/";
            return i;
        } else if (start && ascii_upper(c) == 'R'
                   && ascii_upper(s[i + 1]) == 'E'
                   && ascii_upper(s[i + 2]) == 'M'
                   && !loader_namechar(ascii_upper(s[i + 3]
                                                   ? s[i + 3] : ' '))) {
            return -1;
        } else if (loader_namechar(ascii_upper(c))
                   && !(c >= '0' && c <= '9')) {
            /* a word: after THEN or ELSE a statement starts, as after a
               ':', so IF 1 THEN slash-star is the tokeniser's there */
            char w[5];
            int j = i, k;

            while (s[j] && loader_namechar(ascii_upper(s[j])))
                j++;
            for (k = 0; k < 4 && i + k < j; k++)
                w[k] = ascii_upper(s[i + k]);
            w[k] = 0;
            start = j - i == 4
                    && (strcmp(w, "THEN") == 0 || strcmp(w, "ELSE") == 0);
            i = j - 1;          /* the loop's i++ steps to j */
        } else {
            if (c == '"')
                q = 1;
            else if (c == '\'')
                return -1;
            start = c == ':';
        }
    }
    return -1;
}

/* A line inside a comment block: NULL while the block goes on.  The
 * tokeniser ends it only at a line whose first characters are star-slash,
 * and the manual makes everything after the star-slash comment, so what
 * comes back is the line's number, for a GOTO to it - a numbered
 * star-slash ends the block and keeps its number (FUZIX #24) - or "" for
 * an unnumbered line.  (RC5 still runs a statement after a star-slash and
 * a ':', which the manual does not allow, FUZIX #33.) */
static const char *block_close_rest(const char *s)
{
    int j = 0, n, k = line_start(s);

    while (s[j] == ' ' || s[j] == '\t')
        j++;
    n = j;
    while (s[n] >= '0' && s[n] <= '9')
        n++;
    if (s[k] != '*' || s[k + 1] != '/')
        return NULL;
    return n > j ? sfmt("%.*s ", n - j, s + j) : "";
}

/* What is wrong with the mark block_mark_at found at k, by the manual's
 * rule, or NULL: it must be the first thing on the line, and a space or
 * the end of the line must follow it. */
static const char *block_mark_breaks(const char *s, int k, const char *mark)
{
    char c = s[k + 2];

    if (k != line_start(s))
        return sfmt("%s must be the first thing on its line", mark);
    if (c != 0 && c != ' ' && c != '\t' && c != '\r' && c != '\n')
        return sfmt("%s must have a space or the end of the line after it",
                    mark);
    return NULL;
}

void pass_fonts(void)
{
    int ignore = 0;             /* inside #COMMENT START ... #COMMENT END */
    int debugoff = 1;           /* #MMDEBUG OFF, the loader's default */
    int inblock = 0;            /* inside a slash-star comment block */
    int insub = 0;              /* inside a SUB or FUNCTION */
    unsigned long *words = NULL;
    int nwords = 0, cwords = 0;
    int i = 0;

    while (i < src_nlines) {
        char *line, *up, *p, *num, *u_csub, *sq;
        int start, ended = 0, bad = 0, fno, skip;

        /* the scratch pool is per line here, as it is in tokenize:
         * a 1300-line program would otherwise pile up copies of every
         * line in a 256K process */
        scratch_reset();
        cv.lineno = i + 1;
        line = fstrip(src_lines[i]);
        up = upper(line);
        /* CMM2 LOAD and CMM2 RUN's loader (FileIO.c FileLoadCMM2Program)
         * takes every line that starts with '#' for itself, so none of
         * them is BASIC.  #COMMENT START to #COMMENT END makes the lines
         * between comments, an MMDEBUG statement is one unless #MMDEBUG
         * ON, and a '#' line it does not know is dropped.  #DEFINE,
         * #INCLUDE and #MMDEBUG ON are refused: commented out as
         * statements, their substitutions and files vanished and the
         * code inside a #COMMENT was compiled, with rc 0.  (Plain LOAD
         * and RUN have no '#' lines at all - "Invalid character" - so a
         * program with them is one for that loader.) */
        sq = loader_squash(src_lines[i]);
        if (ignore && strncmp(sq, "#COMMENT END", 12) != 0) {
            src_lines[i] = pstr("");
            i++;
            continue;
        }
        if (sq[0] == '#') {
            const char *w = sq + 1;
            /* only DEFINE is a checkstring, which steps over a blank */
            const char *wd = w[0] == ' ' ? w + 1 : w;

            if (strncmp(wd, "DEFINE", 6) == 0 && !loader_namechar(wd[6]))
                errors_add(sfmt("line %d: #DEFINE is not translated",
                                i + 1));
            else if (strncmp(w, "COMMENT END", 11) == 0)
                ignore = 0;
            else if (strncmp(w, "COMMENT START", 13) == 0)
                ignore = 1;
            else if (strncmp(w, "MMDEBUG ON", 10) == 0) {
                debugoff = 0;
                errors_add(sfmt("line %d: #MMDEBUG ON is not translated",
                                i + 1));
            } else if (strncmp(w, "MMDEBUG OFF", 11) == 0)
                debugoff = 1;
            else if (strncmp(w, "INCLUDE", 7) == 0)
                errors_add(sfmt("line %d: #INCLUDE is not translated",
                                i + 1));
            src_lines[i] = pstr("");
            i++;
            continue;
        }
        if (debugoff && strncmp(sq, "MMDEBUG", 7) == 0
            && !loader_namechar(sq[7])) {
            src_lines[i] = pstr("");
            i++;
            continue;
        }
        /* Comment blocks, which the tokeniser keeps as raw text, so a
         * SUB, a CSUB or a font in one is not there at all (FUZIX issue
         * #19).  The rest of a line after its star-slash statement is
         * BASIC again, and goes round once more as a line of its own. */
        if (inblock) {
            const char *rest = block_close_rest(src_lines[i]);
            const char *bad;

            if (rest == NULL) {
                src_lines[i] = pstr("");
                i++;
                continue;
            }
            bad = block_mark_breaks(src_lines[i], line_start(src_lines[i]),
                                    "*/");
            if (bad != NULL)
                errors_add(sfmt("line %d: %s", i + 1, bad));
            inblock = 0;
            src_lines[i] = pstr(rest);
            i++;
            continue;
        }
        /* where the routines are, for the manual's other rule */
        {
            const char *w = loader_squash(src_lines[i]
                                          + line_start(src_lines[i]));

            if (strncmp(w, "SUB ", 4) == 0 || strncmp(w, "FUNCTION ", 9) == 0)
                insub = 1;
            else if ((strncmp(w, "END SUB", 7) == 0 && !loader_namechar(w[7]))
                     || (strncmp(w, "END FUNCTION", 12) == 0
                         && !loader_namechar(w[12])))
                insub = 0;
        }
        /* Slash-star and star-slash by the manual's rule (FUZIX #33): the
         * first thing on the line, a space or the end of the line after,
         * and no block inside a SUB or FUNCTION.  RC5 takes the other
         * forms in ways the manual does not promise - slash-star and a
         * word is "Invalid character" when it runs, IF 0 THEN slash-star
         * hides nothing - and the firmware is to refuse them when the
         * program is loaded, so they are refused here. */
        {
            const char *mark = NULL;
            int k = block_mark_at(src_lines[i], &mark);

            if (k >= 0) {
                int j, closed = 0;
                char *head = salloc((size_t)k + 40);
                const char *bad = block_mark_breaks(src_lines[i], k, mark);
                char nx = src_lines[i][k + 2];

                if (bad == NULL && mark[0] == '/' && insub)
                    bad = "multi-line comments cannot be used inside "
                          "subroutines and functions";
                if (bad != NULL)
                    errors_add(sfmt("line %d: %s", i + 1, bad));
                memcpy(head, src_lines[i], (size_t)k);
                head[k] = 0;
                if (mark[0] == '*' || loader_namechar(ascii_upper(nx ? nx
                                                                  : ' '))) {
                    /* a star-slash with no block open does nothing, and
                       the rest of its line is comment; slash-star and a
                       word is no command */
                    if (mark[0] == '*')
                        strcat(head, "/*");
                    else
                        head[0] = 0;
                    src_lines[i] = pstr(head);
                    i++;
                    continue;
                }
                for (j = i + 1; j < src_nlines && !closed; j++)
                    if (block_close_rest(src_lines[j]) != NULL)
                        closed = 1;
                /* with no star-slash to come, every line after is
                   comment, and the slash-star itself is cmd_comment's
                   error when it runs; otherwise it stays, a statement
                   that does nothing */
                if (!closed)
                    strcat(head, "Error \"No matching END declaration\"");
                else
                    strcat(head, "/*");
                src_lines[i] = pstr(head);
                inblock = 1;
                i++;
                continue;
            }
        }
        if (strncmp(up, "CSUB", 4) == 0
            && (up[4] == 0 || up[4] == ' ' || up[4] == '\t')) {
            /* A CSUB is machine code for the PicoMite's interpreter, read
             * to its End CSub (MM_Misc.c:9804): nothing here can run it.
             * Said once, and the hex blanked like a font's, or a word
             * such as 1C0569DB reads as line number 1. */
            errors_add(sfmt("line %d: CSUB is not translated: it is "
                            "PicoMite machine code", i + 1));
            src_lines[i] = pstr("");
            i++;
            while (i < src_nlines) {
                char packed[16];
                int j = 0, m;

                scratch_reset();        /* a CSUB runs to hundreds of lines */
                u_csub = upper(fstrip(src_lines[i]));
                for (m = 0; u_csub[m] && j < 15; m++)
                    if (u_csub[m] != ' ' && u_csub[m] != '\t')
                        packed[j++] = u_csub[m];
                packed[j] = 0;
                src_lines[i] = pstr("");
                i++;
                if (strncmp(packed, "ENDCSUB", 7) == 0)
                    break;
            }
            continue;
        }
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

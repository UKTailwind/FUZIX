/* mmbc_main.c - driver: line loading, convert(), argv handling.
 * Mirrors mmb2c.py convert()/main(); --tokens is the stage-1 debug
 * gate shared with the Python.  stdout messages (ERROR/warning/wrote/
 * skipped) are byte-identical to the Python; only usage text names
 * mmbc. */

#include "mmbc.h"

char **src_lines;
int src_nlines;

/* Read the whole file and split into lines, trailing \n kept, exactly
 * like Python readline() in text mode: universal newlines, so \r\n
 * and lone \r both become \n. */
static int load_lines(const char *path, char ***out, int *nout)
{
    FILE *f = fopen(path, "rb");
    char **lines;
    int nlines;
    long sz;
    char *buf;
    char *p, *end;
    int cap = 256;

    if (f == NULL)
        return -1;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = palloc((size_t)sz + 1);
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        return -1;
    }
    fclose(f);
    buf[sz] = 0;

    /* Every byte is LOAD's (FileIO.c): masked to seven bits, and kept
       only if it is then printable, CR, LF or TAB, which becomes a space.
       So a Latin-1 e-acute is an i, a UTF-8 one is two other characters
       and counts as two towards the 255 of a line, and 0x8A and 0x8D end
       a line where they stand (FUZIX #27).  A NUL is dropped like any
       other and LOAD reads on to the end of the file (FUZIX #36): the
       NULs XMODEM pads a last block with - brownian.bas came over with 23
       after "End Sub" - go, and a NUL in the middle stops nothing. */
    {
        long r, w = 0;

        for (r = 0; r < sz; r++) {
            int c = (unsigned char)buf[r] & 0x7F;

            if (c == 9)
                buf[w++] = ' ';
            else if ((c >= 32 && c < 127) || c == 10 || c == 13)
                buf[w++] = (char)c;
        }
        sz = w;
        buf[sz] = 0;
    }

    lines = xrealloc(NULL, sizeof(char *) * (size_t)cap);
    nlines = 0;
    p = buf;
    end = buf + sz;
    while (p < end) {
        char *q = p;
        char *ln;
        size_t len;
        while (q < end && *q != '\n' && *q != '\r')
            q++;
        len = (size_t)(q - p);
        ln = palloc(len + 2);
        memcpy(ln, p, len);
        if (q < end) {                  /* newline present */
            ln[len++] = '\n';
            if (*q == '\r' && q + 1 < end && q[1] == '\n')
                q++;
            q++;
        }
        ln[len] = 0;
        if (nlines == cap) {
            cap *= 2;
            lines = xrealloc(lines, sizeof(char *) * (size_t)cap);
        }
        lines[nlines++] = ln;
        p = q;
    }
    *out = lines;
    *nout = nlines;
    return 0;
}

static int read_lines(const char *path)
{
    return load_lines(path, &src_lines, &src_nlines);
}

/* The program's lines; a library's (library_splice) follow them. */
int src_nprog;

/*
 * #INCLUDE and #DEFINE, as the CMM2 loader has them (FileIO.c
 * FileLoadCMM2Program and importfile), before any pass sees the lines:
 * mmb2c.py's loader_directives, line for line - see there.  The errors
 * wait in ld_errs until convert() has a cv to put them in.
 */
#define LOADER_MAXDEFINES 16    /* the firmware's MAXDEFINES */
#define LOADER_DEFLEN 32        /* sizeof(dlist[0].from) and .to */

static char ld_from[LOADER_MAXDEFINES][LOADER_DEFLEN];
static char ld_to[LOADER_MAXDEFINES][LOADER_DEFLEN];
static int ld_ndefs;
static char **ld_out;
static int ld_nout, ld_cout;
static char **ld_errs;
static int ld_nerrs, ld_cerrs;
static char *ld_here;           /* the program's directory, its / kept */

static char ld_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

static void ld_put(char *ln)
{
    GROW(ld_out, ld_nout, ld_cout);
    ld_out[ld_nout++] = ln;
}

static void ld_err(const char *msg)
{
    GROW(ld_errs, ld_nerrs, ld_cerrs);
    ld_errs[ld_nerrs++] = pstr(msg);
}

/* the loader's blanks: none at the start, and outside quotes one after
 * a blank or a colon where there were several, or none after a colon */
static char *loader_collapse(const char *s)
{
    char *o = salloc(strlen(s) + 1);
    int n = 0, q = 0;

    while (*s == ' ')
        s++;
    while (*s) {
        char c = *s++;

        if (c == '"')
            q = !q;
        o[n++] = c;
        if (!q && (c == ' ' || c == ':'))
            while (*s == ' ')
                s++;
    }
    o[n] = 0;
    return o;
}

/* isnameend: a name character, or a type suffix (0 the end) */
static int loader_nameend(char c)
{
    return c != 0 && (loader_namechar(c) || c == '$' || c == '!'
                      || c == '%');
}

/* STR_REPLACE as massage() calls it - mmb2c.py's loader_replace */
static char *loader_replace(char *line, const char *needle,
                            const char *repl)
{
    size_t len = strlen(line), n = strlen(needle), rl = strlen(repl);
    size_t k = 0, i, m = 0, pos = 0, at;
    const char *comment = "";
    char *t, *tu, *out;
    int q = 0, found;

    while (line[k] == ' ')
        k++;
    if (ld_upper(line[k]) == 'R' && ld_upper(line[k + 1]) == 'E'
        && ld_upper(line[k + 2]) == 'M'
        && !loader_namechar(ld_upper(line[k + 3])))
        return line;
    /* no occurrence at all, in any case: nothing to do (and nothing to
     * allocate, sixteen #DEFINEs a line) */
    for (at = 0, found = 0; at + n <= len && !found; at++) {
        for (i = 0; i < n && ld_upper(line[at + i]) == needle[i]; i++)
            ;
        found = i == n;
    }
    if (!found)
        return line;
    t = salloc(len + 1);
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)line[i];

        if (c == '"') {
            q = !q;
            t[m++] = (char)c;
            continue;
        }
        if (!q && c == '\'') {
            comment = line + i;
            break;
        }
        /* a quoted character is hidden from the search, as the firmware
         * hides it by setting its top bit */
        t[m++] = (char)(q ? (c | 0x80) : c);
    }
    t[m] = 0;
    tu = salloc(m + 1);
    for (i = 0; i <= m; i++)
        tu[i] = ld_upper(t[i]);
    out = salloc(m + (m / n + 1) * rl + strlen(comment) + 1);
    k = 0;
    for (;;) {
        for (at = pos, found = 0; at + n <= m; at++)
            if (memcmp(tu + at, needle, n) == 0) {
                found = 1;
                break;
            }
        if (!found) {
            memcpy(out + k, t + pos, m - pos);
            k += m - pos;
            break;
        }
        memcpy(out + k, t + pos, at - pos);
        k += at - pos;
        if ((at == 0 || !loader_namechar(tu[at - 1]))
            && !loader_nameend(tu[at + n])) {
            memcpy(out + k, repl, rl);
            k += rl;
        } else {
            memcpy(out + k, t + at, n);
            k += n;
        }
        pos = at + n;
    }
    for (i = 0; i < k; i++)
        out[i] &= 0x7F;
    strcpy(out + k, comment);
    return out;
}

/* count string literals separated by commas, and nothing after them
 * but blanks or a ' comment: 1 with their contents in got[], or 0 */
static int loader_strings(const char *s, int count, char **got)
{
    int k;

    for (k = 0; k < count; k++) {
        const char *e;

        while (*s == ' ')
            s++;
        if (k) {
            if (*s != ',')
                return 0;
            s++;
            while (*s == ' ')
                s++;
        }
        if (*s != '"')
            return 0;
        e = strchr(s + 1, '"');
        if (e == NULL)
            return 0;
        got[k] = salloc((size_t)(e - s));
        memcpy(got[k], s + 1, (size_t)(e - s - 1));
        got[k][e - s - 1] = 0;
        s = e + 1;
    }
    while (*s == ' ')
        s++;
    return *s == 0 || *s == '\'';
}

/* the text after the first `word' in the line, as written */
static const char *ld_after(const char *raw, const char *word)
{
    char *ru = sstr(raw);
    char *f;
    size_t i;

    for (i = 0; ru[i]; i++)
        ru[i] = ld_upper(ru[i]);
    f = strstr(ru, word);
    return raw + (f - ru) + strlen(word);
}

static void ld_walk(char **src, int n, int inc);

/* 0 not a '#' line; 1 a '#' line, kept; 2 an #INCLUDE done */
static int ld_directive(char *ln, int inc)
{
    const char *sq = loader_squash(ln), *w, *wd;
    char *got[2], *name;
    int at = ld_nout + 1, k, dot;

    if (sq[0] != '#')
        return 0;
    w = sq + 1;
    wd = w[0] == ' ' ? w + 1 : w;
    if (strncmp(wd, "DEFINE", 6) == 0 && !loader_namechar(wd[6])) {
        if (!loader_strings(ld_after(stripped(ln), "DEFINE"), 2, got))
            ld_err(sfmt("line %d: #DEFINE wants two strings", at));
        else if (ld_ndefs >= LOADER_MAXDEFINES)
            ld_err(sfmt("line %d: Too many #DEFINE statements", at));
        else if (strlen(got[0]) >= LOADER_DEFLEN
                 || strlen(got[1]) >= LOADER_DEFLEN)
            ld_err(sfmt("line %d: #DEFINE string too long", at));
        else if (got[0][0]) {
            for (k = 0; got[0][k]; k++)
                ld_from[ld_ndefs][k] = ld_upper(got[0][k]);
            ld_from[ld_ndefs][k] = 0;
            for (k = 0; got[1][k]; k++)
                ld_to[ld_ndefs][k] = ld_upper(got[1][k]);
            ld_to[ld_ndefs][k] = 0;
            ld_ndefs++;
        }
        return 1;
    }
    if (strncmp(w, "INCLUDE", 7) != 0)
        return 1;
    if (inc) {
        ld_err(sfmt("line %d: Can't import from an import", at));
        return 1;
    }
    if (!loader_strings(ld_after(stripped(ln), "INCLUDE"), 1, got)) {
        ld_err(sfmt("line %d: #INCLUDE wants a file name in quotes", at));
        return 1;
    }
    name = got[0];
    dot = 0;                    /* a dot after the last slash */
    for (k = 0; name[k]; k++)
        if (name[k] == '/' || name[k] == '\\')
            dot = 0;
        else if (name[k] == '.')
            dot = 1;
    if (!dot)
        name = sfmt("%s.inc", name);
    k = (int)strlen(name);
    if (k < 4 || name[k - 4] != '.' || ld_upper(name[k - 3]) != 'I'
        || ld_upper(name[k - 2]) != 'N' || ld_upper(name[k - 1]) != 'C') {
        ld_err(sfmt("line %d: #INCLUDE: must be a .inc file", at));
        return 1;
    }
    if (name[0] && name[1] == ':') {
        ld_err(sfmt("line %d: #INCLUDE with a drive is not translated",
                    at));
        return 1;
    }
    if (name[0] != '/' && name[0] != '\\')
        name = sfmt("%s%s", ld_here, name);
    {
        char **lines;
        int nl;

        name = pstr(name);
        if (load_lines(name, &lines, &nl) != 0) {
            ld_err(sfmt("line %d: #INCLUDE: cannot read %s", at, name));
            return 1;
        }
        if (nl > 0) {
            size_t ln2 = strlen(lines[nl - 1]);

            if (ln2 == 0 || lines[nl - 1][ln2 - 1] != '\n')
                lines[nl - 1] = pstr(sfmt("%s\n", lines[nl - 1]));
        }
        ld_put(ln);
        ld_walk(lines, nl, 1);
    }
    return 2;
}

static void ld_walk(char **src, int n, int inc)
{
    int i, j, d, ignore = 0;

    for (i = 0; i < n; i++) {
        char *ln = src[i];
        const char *sq;

        scratch_reset();
        sq = loader_squash(ln);
        if (ignore) {
            if (strncmp(sq, "#COMMENT END", 12) == 0)
                ignore = 0;
            ld_put(ln);
            continue;
        }
        if (strncmp(sq, "#COMMENT START", 14) == 0)
            ignore = 1;
        d = ld_directive(ln, inc);
        if (d == 2)
            continue;           /* the #INCLUDE, and the file after it */
        if (d || ld_ndefs == 0) {
            ld_put(ln);
            continue;
        }
        {
            size_t len = strlen(ln);
            int nl = len > 0 && ln[len - 1] == '\n';
            char *body = salloc(len + 1), *flat, *nw;

            memcpy(body, ln, len - (size_t)nl);
            body[len - (size_t)nl] = 0;
            flat = loader_collapse(body);
            nw = flat;
            for (j = ld_ndefs - 1; j >= 0; j--)
                nw = loader_replace(nw, ld_from[j], ld_to[j]);
            if (strcmp(nw, flat) != 0)
                ld_put(pstr(sfmt("%s%s", nw, nl ? "\n" : "")));
            else
                ld_put(ln);
        }
    }
}

static void loader_directives(const char *inpath)
{
    int k, cut = -1;

    for (k = 0; inpath[k]; k++)
        if (inpath[k] == '/' || inpath[k] == '\\')
            cut = k;
    ld_here = pstr(sfmt("%.*s", cut + 1, inpath));
    ld_walk(src_lines, src_nlines, 0);
    src_lines = ld_out;
    src_nlines = ld_nout;
    scratch_reset();
}

/* The line the passes take pos-th: a library's lines first, then the
 * program's - mmb2c.py's Conv.order. */
int src_line_at(int pos)
{
    int nlib = src_nlines - src_nprog;

    if (nlib <= 0)
        return pos;
    return pos < nlib ? src_nprog + pos : pos - nlib;
}

/*
 * LIBRARY LOAD file$ [, file$ ...] [, O] [, RAM] - MM_Misc.c's
 * cmd_library: the files' source becomes the program's library, whose
 * top level runs at RUN before the program's first line.  It must be
 * the program's first statement (comments, blank lines and OPTIONs may
 * come before it).  mmb2c.py's library_splice, line for line.
 *
 * The library is translated with the program.  Its lines go AFTER the
 * program's, so every program line keeps its number in messages, and
 * src_line_at walks them FIRST, so its CONSTs and DIMs come before the
 * program's code as on the firmware.  The LIBRARY LOAD line is left
 * blank.  A file name the translator can read is a string, or
 * MM.INFO(PATH) + a string - the program's own directory.
 *
 * Returns NULL, or the refusal's text.
 */
#define LIB_MAXARGS 16

static const char *library_splice(const char *inpath)
{
    int i = 0, k, cut, nargs = 0, nnames = 0, q = 0;
    const char *s, *raw;
    char *args[LIB_MAXARGS], *names[LIB_MAXARGS], *cur;
    char **lib = NULL, **all;
    int nlib = 0;

    src_nprog = src_nlines;
    while (i < src_nlines) {
        s = loader_squash(src_lines[i] + line_start(src_lines[i]));
        if (s[0] == 0 || s[0] == '\''
            || (strncmp(s, "REM", 3) == 0 && !loader_namechar(s[3]))
            || (strncmp(s, "OPTION", 6) == 0 && !loader_namechar(s[6]))) {
            i++;
            continue;
        }
        break;
    }
    if (i >= src_nlines)
        return NULL;
    s = loader_squash(src_lines[i] + line_start(src_lines[i]));
    if (!(strncmp(s, "LIBRARY LOAD", 12) == 0 && !loader_namechar(s[12])))
        return NULL;
    /* the arguments, as written (a file name keeps its case) */
    raw = src_lines[i] + line_start(src_lines[i]);
    while (*raw == ' ' || *raw == '\t')
        raw++;
    k = 0;
    {
        static const char *const words[2] = { "LIBRARY", "LOAD" };
        int w;

        for (w = 0; w < 2; w++) {
            while (raw[k] == ' ' || raw[k] == '\t')
                k++;
            k += (int)strlen(words[w]);
        }
    }
    cur = sstr("");
    for (; raw[k] && raw[k] != '\n' && raw[k] != '\r'; k++) {
        char c = raw[k];

        if (c == '"')
            q = !q;
        if (c == ',' && !q) {
            if (nargs < LIB_MAXARGS)
                args[nargs++] = stripped(cur);
            cur = sstr("");
        } else
            cur = sfmt("%s%c", cur, c);
    }
    if (nargs < LIB_MAXARGS)
        args[nargs++] = stripped(cur);
    cut = -1;
    for (k = 0; inpath[k]; k++)
        if (inpath[k] == '/' || inpath[k] == '\\')
            cut = k;
    for (k = 0; k < nargs; k++) {
        const char *a = args[k], *au = loader_squash(a);
        char *compact, *cu;
        size_t n = strlen(a), j, m = 0;
        static const char *const pre[2] = { "MM.INFO(PATH)+\"",
                                            "MM.INFO$(PATH)+\"" };
        int pk, found = 0;

        if (strcmp(au, "O") == 0 || strcmp(au, "OVERWRITE") == 0
            || strcmp(au, "RAM") == 0)
            continue;
        compact = salloc(n + 1);
        cu = salloc(n + 1);
        for (j = 0; j < n; j++)
            if (a[j] != ' ' && a[j] != '\t') {
                compact[m] = a[j];
                cu[m] = (a[j] >= 'a' && a[j] <= 'z') ? a[j] - 32 : a[j];
                m++;
            }
        compact[m] = cu[m] = 0;
        if (n >= 2 && a[0] == '"' && a[n - 1] == '"') {
            if (nnames < LIB_MAXARGS)
                names[nnames++] = pstr(sfmt("%.*s%.*s", cut + 1, inpath,
                                            (int)n - 2, a + 1));
            continue;
        }
        for (pk = 0; pk < 2; pk++) {
            size_t pl = strlen(pre[pk]);

            if (strncmp(cu, pre[pk], pl) == 0 && m > pl
                && compact[m - 1] == '"') {
                if (nnames < LIB_MAXARGS)
                    names[nnames++] = pstr(sfmt("%.*s%.*s", cut + 1, inpath,
                                                (int)(m - pl - 1),
                                                compact + pl));
                found = 1;
                break;
            }
        }
        if (!found)
            return pstr(sfmt("line %d: LIBRARY LOAD: a file name the "
                             "translator can read is a string, or "
                             "MM.INFO(PATH) + a string", i + 1));
    }
    for (k = 0; k < nnames; k++) {
        char **got;
        int ng, j;

        if (load_lines(names[k], &got, &ng) != 0)
            return pstr(sfmt("line %d: LIBRARY LOAD: cannot read %s", i + 1,
                             names[k]));
        if (ng > 0) {
            size_t ln = strlen(got[ng - 1]);

            if (ln == 0 || got[ng - 1][ln - 1] != '\n')
                got[ng - 1] = pstr(sfmt("%s\n", got[ng - 1]));
        }
        lib = xrealloc(lib, sizeof(char *) * (size_t)(nlib + ng + 1));
        for (j = 0; j < ng; j++)
            lib[nlib++] = got[j];
    }
    all = xrealloc(NULL, sizeof(char *) * (size_t)(src_nlines + nlib + 1));
    for (k = 0; k < src_nlines; k++)
        all[k] = src_lines[k];
    all[i] = pstr("\n");
    for (k = 0; k < nlib; k++)
        all[src_nlines + k] = lib[k];
    src_lines = all;
    src_nprog = src_nlines;
    src_nlines += nlib;
    return NULL;
}

/* Debug aid: fixed-format token stream, byte-diffed against
 * `mmb2c.py --tokens` by mmbc/tokgate.sh. */
static int dump_tokens(const char *inpath)
{
    static struct tok toks[MAXTOKS];
    int idx, k, nt;

    if (read_lines(inpath) != 0) {
        fprintf(stderr, "mmbc: cannot read %s\n", inpath);
        return 1;
    }
    scan_escape();
    for (idx = 0; idx < src_nlines; idx++) {
        int lineno = idx + 1;
        jmp_buf jb, *saved = err_jmp;
        err_jmp = &jb;
        if (setjmp(jb) == 0) {
            nt = tokenize(src_lines[idx], lineno, toks);
            err_jmp = saved;
        } else {
            err_jmp = saved;
            printf("ERR %d %s\n", lineno, err_msg);
            continue;
        }
        for (k = 0; k < nt; k++)
            printf("%d %d [%s] [%s]\n", lineno, toks[k].kind,
                   toks[k].text, toks[k].up);
    }
    return 0;
}

/* mmb2c.py convert(): returns the output path, NULL on errors. */
static const char *convert(const char *inpath, const char *outpath,
                           int report, int lenient, int fcc)
{
    const char **const_accs;
    const char *liberr;
    int k;
    FILE *of;
    struct outbuf rep;

    if (read_lines(inpath) != 0) {
        fprintf(stderr, "mmbc: cannot read %s\n", inpath);
        return NULL;
    }
    loader_directives(inpath);
    liberr = library_splice(inpath);
    scan_escape();                /* OPTION ESCAPE's positional gate */
    memset(&cv, 0, sizeof(cv));
    cv.srcname = inpath;
    cv.lenient = lenient;
    cv.fcc = fcc;
    cv.opt_default = TY_F;
    cv.mode = M_SCAN;
    cv.indent = 1;
    for (k = 0; k < ld_nerrs; k++)
        errors_add(ld_errs[k]);
    if (liberr != NULL)
        errors_add(liberr);

    /* before every other pass: a DefineFont block is hex, not BASIC,
     * and is in force from the top of the program however far down it
     * sits */
    pass_fonts();
    pass_routine_names();
    pass_types();
    pass_declarations();
    walk(M_SCAN);
    close_writes();     /* the writes routines make, with their callees' */
    /* constants become #define, so fix their access text before
     * emitting, and put it back for the #define bodies afterwards */
    const_accs = palloc(sizeof(char *)
                        * (size_t)(cv.nglobals ? cv.nglobals : 1));
    for (k = 0; k < cv.nglobals; k++) {
        const_accs[k] = cv.globals[k]->acc;
        if (cv.globals[k]->is_const)
            cv.globals[k]->acc = pstr(cconst(cv.globals[k]->name));
    }
    /* Arrays and strings live in the struct global_decls builds, so
     * every reference to one goes through H.  The emitter reads acc for
     * every read and write of a variable, so rewriting it here IS the
     * change - nothing in the expression or statement code has to know.
     * Done between the scan and emit passes, exactly as the const
     * rewrite above. */
    for (k = 0; k < cv.nglobals; k++) {
        struct sym *s = cv.globals[k];
        if (s->is_const)
            continue;
        if (s->is_array || s->ty == TY_S || s->stype != NULL) {
            s->acc = pstr(sfmt("H->%s", cvar(s->name)));
            if (s->dynamic)
                s->bacc = pstr(sfmt("H->__b_%s", cvar(s->name)));
        }
    }
    /* The same rewrite per routine for LOCAL arrays and strings, which
     * live in a block taken per invocation - that is what makes
     * recursion correct, each call having its own.  STATIC locals keep
     * their C storage: they outlive the invocation by definition.
     * Parameters are the caller's and are not ours to move. */
    for (k = 0; k < cv.nroutines; k++) {
        struct routine *r = cv.routines[k];
        int j;
        for (j = 0; j < r->nlocal_order; j++) {
            struct sym *s = NULL;
            int q;
            for (q = 0; q < r->nlocals; q++)
                if (strcmp(r->locals[q]->name, r->local_order[j]) == 0) {
                    s = r->locals[q];
                    break;
                }
            /* A STATIC whose bounds are worked out when it is made keeps
               its pointer and bounds table in the routine's static storage
               (emit_local_decl, FUZIX #31). */
            if (s != NULL && s->is_static && s->is_array && s->dynamic)
                s->bacc = pstr(sfmt("__b_%s", cvar(s->name)));
            if (s == NULL || s->is_param || s->is_static)
                continue;
            if (!(s->is_array || s->ty == TY_S || s->stype != NULL))
                continue;
            r->heap_locals = 1;
            s->acc = pstr(sfmt("__L->%s", cvar(s->name)));
            if (s->dynamic)
                s->bacc = pstr(sfmt("__L->__b_%s", cvar(s->name)));
        }
    }
    cv.tmpn = 0;
    cv.out_main.n = 0;
    cv.out_body.n = 0;
    walk(M_EMIT);
    for (k = 0; k < cv.nglobals; k++)
        if (cv.globals[k]->is_const)
            cv.globals[k]->acc = const_accs[k];

    if (cv.nerrors > 0) {
        for (k = 0; k < cv.nerrors; k++)
            printf("ERROR %s\n", cv.errors[k]);
        return NULL;
    }

    if (outpath == NULL) {
        size_t n = strlen(inpath);
        char *op;
        int isbas = n >= 4
            && (inpath[n - 4] == '.')
            && (inpath[n - 3] == 'b' || inpath[n - 3] == 'B')
            && (inpath[n - 2] == 'a' || inpath[n - 2] == 'A')
            && (inpath[n - 1] == 's' || inpath[n - 1] == 'S');
        if (isbas) {
            op = palloc(n + 1);
            memcpy(op, inpath, n - 4);
            strcpy(op + n - 4, ".c");
        } else {
            op = palloc(n + 3);
            strcpy(op, inpath);
            strcat(op, ".c");
        }
        outpath = op;
    }
    of = fopen(outpath, "wb");
    if (of == NULL) {
        fprintf(stderr, "mmbc: cannot write %s\n", outpath);
        return NULL;
    }
    conv_write(of);
    fclose(of);

    if (cv.nskipped > 0) {
        printf("%d line(s) could not be translated and were "
               "commented out:\n", cv.nskipped);
        /* The offending source, not just the complaint.  It was already
           being kept for the report at the end of the C and simply
           never shown here, so a message like "expected ')'" arrived
           with nothing to attach it to - on a board, where the source
           is a file away, that is most of the diagnosis missing. */
        for (k = 0; k < cv.nskipped; k++) {
            printf("  line %d: %s\n", cv.skipped[k].line,
                   cv.skipped[k].why);
            if (cv.skipped[k].text && cv.skipped[k].text[0])
                printf("      %.72s\n", cv.skipped[k].text);
        }
    }
    if (report) {
        memset(&rep, 0, sizeof(rep));
        report_build(&rep);
        for (k = 0; k < rep.n; k++)
            printf("%s\n", rep.lines[k]);
    }
    for (k = 0; k < cv.nwarnings; k++)
        printf("warning: %s\n", cv.warnings[k]);
    return outpath;
}

int main(int argc, char **argv)
{
    const char *src = NULL;
    const char *dst = NULL;
    const char *out;
    /*
     * On the board the only compiler is the Fuzix one, which is C89:
     * the gcc-shaped output uses compound literals for array bounds
     * and will not compile there.  So the board build translates for
     * its own compiler unless told otherwise, and --gcc asks for the
     * host form.  The host build keeps the old default so the gates,
     * which pass the mode explicitly, compare like with like.
     */
#ifdef MMBC_ARENA
    int fcc = 1;
#else
    int fcc = 0;
#endif
    int rep = 0, strict = 0, tokens = 0;
    int k;

    for (k = 1; k < argc; k++) {
        if (strcmp(argv[k], "-o") == 0 && k + 1 < argc) {
            dst = argv[++k];
        } else if (strcmp(argv[k], "--report") == 0) {
            rep = 1;
        } else if (strcmp(argv[k], "--strict") == 0) {
            strict = 1;
        } else if (strcmp(argv[k], "--fcc") == 0) {
            fcc = 1;
        } else if (strcmp(argv[k], "--gcc") == 0) {
            fcc = 0;
        } else if (strcmp(argv[k], "--tokens") == 0) {
            tokens = 1;
        } else if (strcmp(argv[k], "-h") == 0
                   || strcmp(argv[k], "--help") == 0) {
            printf("usage: mmbc source.bas [-o out.c] [--report] "
                   "[--strict] [--fcc]\n");
            printf("  --report  list implied globals and skipped lines\n");
            printf("  --strict  stop on anything that cannot be "
                   "translated,\n");
            printf("            instead of commenting it out and "
                   "carrying on\n");
            printf("  --fcc     C89 output for the Fuzix C compiler: "
                   "no\n");
            printf("            compound literals%s\n",
#ifdef MMBC_ARENA
                   " (the default here)");
#else
                   "");
#endif
            printf("  --gcc     C99 output for a host compiler%s\n",
#ifdef MMBC_ARENA
                   "");
#else
                   " (the default here)");
#endif
            return 0;
        } else {
            src = argv[k];
        }
    }
    if (src == NULL) {
        printf("usage: mmbc source.bas [-o out.c] [--report] [--strict] "
               "[--fcc]\n");
        return 1;
    }
    if (tokens)
        return dump_tokens(src);
    out = convert(src, dst, rep, !strict, fcc);
    if (out == NULL)
        return 2;
    printf("wrote %s\n", out);
    return 0;
}

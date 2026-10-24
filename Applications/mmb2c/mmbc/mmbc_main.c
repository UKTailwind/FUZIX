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
    liberr = library_splice(inpath);
    scan_escape();                /* OPTION ESCAPE's positional gate */
    memset(&cv, 0, sizeof(cv));
    cv.srcname = inpath;
    cv.lenient = lenient;
    cv.fcc = fcc;
    cv.opt_default = TY_F;
    cv.mode = M_SCAN;
    cv.indent = 1;
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

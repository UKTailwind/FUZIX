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
static int read_lines(const char *path)
{
    FILE *f = fopen(path, "rb");
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

    src_lines = xrealloc(NULL, sizeof(char *) * (size_t)cap);
    src_nlines = 0;
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
        if (src_nlines == cap) {
            cap *= 2;
            src_lines = xrealloc(src_lines,
                                 sizeof(char *) * (size_t)cap);
        }
        src_lines[src_nlines++] = ln;
        p = q;
    }
    return 0;
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
    int k;
    FILE *of;
    struct outbuf rep;

    if (read_lines(inpath) != 0) {
        fprintf(stderr, "mmbc: cannot read %s\n", inpath);
        return NULL;
    }
    memset(&cv, 0, sizeof(cv));
    cv.srcname = inpath;
    cv.lenient = lenient;
    cv.fcc = fcc;
    cv.opt_default = TY_F;
    cv.mode = M_SCAN;
    cv.indent = 1;

    pass_routine_names();
    pass_declarations();
    walk(M_SCAN);
    /* constants become #define, so fix their access text before
     * emitting, and put it back for the #define bodies afterwards */
    const_accs = palloc(sizeof(char *)
                        * (size_t)(cv.nglobals ? cv.nglobals : 1));
    for (k = 0; k < cv.nglobals; k++) {
        const_accs[k] = cv.globals[k]->acc;
        if (cv.globals[k]->is_const)
            cv.globals[k]->acc = pstr(cvar(cv.globals[k]->name));
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
        for (k = 0; k < cv.nskipped; k++)
            printf("  line %d: %s\n", cv.skipped[k].line,
                   cv.skipped[k].why);
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

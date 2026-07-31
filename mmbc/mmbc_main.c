/* mmbc_main.c - driver.  Stage 1: line loading and --tokens only;
 * convert() arrives with stage 6. */

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

    src_lines = malloc(sizeof(char *) * (size_t)cap);
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
            src_lines = realloc(src_lines, sizeof(char *) * (size_t)cap);
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

int main(int argc, char **argv)
{
    const char *src = NULL;
    int tokens = 0;
    int k;

    for (k = 1; k < argc; k++) {
        if (strcmp(argv[k], "--tokens") == 0)
            tokens = 1;
        else
            src = argv[k];
    }
    if (src == NULL) {
        fprintf(stderr, "usage: mmbc source.bas --tokens\n");
        return 1;
    }
    if (tokens)
        return dump_tokens(src);
    fprintf(stderr, "mmbc: stage 1 - only --tokens is implemented yet\n");
    return 1;
}

/* mmbc_util.c - bump pools, scratch sprintf, MMError machinery. */

#include "mmbc.h"

/* ---- persistent pool: chained blocks, never freed (the Python never
 * frees either).  Host: malloc.  Board: first block comes from the
 * PSRAM arena - same chaining, different block source (stage 7). ---- */

#define PBLOCK (64 * 1024)

struct pblk {
    struct pblk *next;
    size_t used, size;
    /* data follows */
};

static struct pblk *phead;

void *palloc(size_t n)
{
    struct pblk *b = phead;
    char *p;
    size_t sz;

    n = (n + 7) & ~(size_t)7;
    if (b == NULL || b->used + n > b->size) {
        sz = (n > PBLOCK) ? n : PBLOCK;
        b = malloc(sizeof(struct pblk) + sz);
        if (b == NULL) {
            fprintf(stderr, "mmbc: out of memory\n");
            exit(2);
        }
        b->next = phead;
        b->used = 0;
        b->size = sz;
        phead = b;
    }
    p = (char *)(b + 1) + b->used;
    b->used += n;
    return p;
}

char *pstr(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = palloc(n);
    memcpy(p, s, n);
    return p;
}

/* ---- scratch pool: one block, reset at each tokenize() ---- */

#define SBLOCK (256 * 1024)

static char *sbase;
static size_t sused;

void *salloc(size_t n)
{
    char *p;

    n = (n + 7) & ~(size_t)7;
    if (sbase == NULL) {
        sbase = malloc(SBLOCK);
        if (sbase == NULL) {
            fprintf(stderr, "mmbc: out of memory\n");
            exit(2);
        }
    }
    if (sused + n > SBLOCK)
        mm_error("line ?: statement too complex (scratch pool)");
    p = sbase + sused;
    sused += n;
    return p;
}

char *sstr(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = salloc(n);
    memcpy(p, s, n);
    return p;
}

void scratch_reset(void)
{
    sused = 0;
}

char *sfmt(const char *fmt, ...)
{
    va_list ap;
    int n;
    char *p;

    va_start(ap, fmt);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    p = salloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(p, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return p;
}

/* ---- MMError ---- */

jmp_buf *err_jmp;
char err_msg[512];

void mm_error(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(err_msg, sizeof(err_msg), fmt, ap);
    va_end(ap);
    if (err_jmp == NULL) {
        /* stands in for an uncaught Python traceback */
        fprintf(stderr, "mmbc: MMError: %s\n", err_msg);
        exit(2);
    }
    longjmp(*err_jmp, 1);
}

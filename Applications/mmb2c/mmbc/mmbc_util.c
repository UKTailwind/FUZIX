/* mmbc_util.c - bump pools, scratch sprintf, MMError machinery.
 *
 * Two builds share this file.  Host: pools sit on malloc.  Board
 * (-DMMBC_ARENA): a single PSRAM arena allocation backs everything -
 * pools, and xrealloc via size headers with extend-in-place for the
 * most recent block - because a 256K Fuzix process cannot hold the
 * working set (mmbc is the arena's second client after cc2, see
 * PC3-PSRAM-ARENA.md).  The arena is released by the kernel on exit. */

#include "mmbc.h"

#ifdef MMBC_ARENA

#include <fcntl.h>
#include <sys/ioctl.h>

#define PSRAMIOC_ALLOC	0x000A
struct psram_req {
    unsigned long len;
    unsigned long base;
};

/* 768K translated everything up to the eclipse; PicoMan (77K of
 * BASIC, the first Game*Mite-sized program translated ON the board)
 * exhausted it.  The allocator is copy-and-abandon by design, so the
 * bound is generous rather than tight - the PSRAM heap is ~7.8M and
 * the one-compile-at-a-time rule means no concurrent claimant. */
#define MMBC_ARENA_LEN (2048UL * 1024)

static unsigned char *ar_cur, *ar_end;

static void ar_init(void)
{
    struct psram_req rq;
    int fd;

    if (ar_cur != NULL)
        return;
    fd = open("/dev/sys", O_RDWR);
    rq.len = MMBC_ARENA_LEN;
    if (fd < 0 || ioctl(fd, PSRAMIOC_ALLOC, &rq) < 0) {
        fprintf(stderr, "mmbc: no PSRAM arena (kernel without "
                        "PSRAMIOC_ALLOC?)\n");
        exit(1);
    }
    close(fd);
    ar_cur = (unsigned char *)rq.base;
    ar_end = ar_cur + MMBC_ARENA_LEN;
}

static void *ar_carve(size_t n)
{
    void *p;

    ar_init();
    n = (n + 7) & ~(size_t)7;
    if (ar_cur + n > ar_end) {
        fprintf(stderr, "mmbc: arena exhausted\n");
        exit(2);
    }
    p = ar_cur;
    ar_cur += n;
    return p;
}

#endif /* MMBC_ARENA */

/* ---- persistent pool: chained blocks, never freed (the Python never
 * frees either). ---- */

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
#ifdef MMBC_ARENA
        b = ar_carve(sizeof(struct pblk) + sz);
#else
        b = malloc(sizeof(struct pblk) + sz);
        if (b == NULL) {
            fprintf(stderr, "mmbc: out of memory\n");
            exit(2);
        }
#endif
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

/* Sized for the biggest single-line churn plus a whole write() phase
 * (no tokenize reset runs during output assembly). */
#ifdef MMBC_ARENA
#define SBLOCK (128 * 1024)
#else
#define SBLOCK (512 * 1024)
#endif

static char *sbase;
static size_t sused;

static void s_init(void)
{
    if (sbase != NULL)
        return;
#ifdef MMBC_ARENA
    sbase = ar_carve(SBLOCK);
#else
    sbase = malloc(SBLOCK);
    if (sbase == NULL) {
        fprintf(stderr, "mmbc: out of memory\n");
        exit(2);
    }
#endif
}

void *salloc(size_t n)
{
    char *p;

    n = (n + 7) & ~(size_t)7;
    s_init();
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

/* Single pass straight into the scratch tail.  No vsnprintf(NULL, 0)
 * measure - the Fuzix libc's vsnprintf returns the TRUNCATED count,
 * not the C99 would-be length, so the two-pass idiom silently breaks
 * there.  The overflow guard fires on both return conventions. */
char *sfmt(const char *fmt, ...)
{
    va_list ap;
    size_t avail;
    int n;
    char *p;

    s_init();
    avail = SBLOCK - sused;
    if (avail < 2)
        mm_error("line ?: statement too complex (scratch pool)");
    p = sbase + sused;
    va_start(ap, fmt);
    n = vsnprintf(p, avail, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= avail - 1)
        mm_error("line ?: statement too complex (scratch pool)");
    sused += ((size_t)n + 1 + 7) & ~(size_t)7;
    return p;
}

#ifdef MMBC_ARENA

/* Arena realloc: an 8-byte size header ahead of each block (union, so
 * user pointers stay 8-aligned on ILP32); the most recent block
 * extends in place (the doubling GROW arrays usually are), anything
 * else is copy-and-abandon - the arena is big and dies with the
 * process. */
union ar_hdr { size_t size; unsigned char pad[8]; };

void *xrealloc(void *p, size_t n)
{
    union ar_hdr *h;
    size_t old;
    void *q;

    n = (n + 7) & ~(size_t)7;
    if (p != NULL) {
        h = (union ar_hdr *)p - 1;
        old = h->size;
        if ((unsigned char *)p + old == ar_cur) {
            if ((unsigned char *)p + n > ar_end) {
                fprintf(stderr, "mmbc: arena exhausted\n");
                exit(2);
            }
            ar_cur = (unsigned char *)p + n;
            h->size = n;
            return p;
        }
    }
    h = ar_carve(n + sizeof(union ar_hdr));
    h->size = n;
    q = h + 1;
    if (p != NULL) {
        old = ((union ar_hdr *)p - 1)->size;
        memcpy(q, p, old);
    }
    return q;
}

#else

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n);
    if (p == NULL) {
        fprintf(stderr, "mmbc: out of memory\n");
        exit(2);
    }
    return p;
}

#endif

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

/* psbench - how fast can we shuffle bytes about in the PSRAM arena?
 *
 * The MMBasic editor keeps the whole file in one flat buffer and
 * memmoves the tail on every insert and delete.  That is fine in
 * MMBasic, whose buffer is ~96K of SRAM.  If the buffer lives in the
 * PSRAM arena instead, the cost per keystroke is whatever this
 * measures - and if it is too slow the editor needs a gap buffer.
 *
 * Measures the editor's actual worst case: memmove(p+1, p, n), an
 * overlapping backward move at a one-byte offset, which defeats any
 * word-at-a-time fast path the library might have.  Aligned moves and
 * an SRAM baseline are measured alongside for context.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "../pico_ioctl.h"

static int fd;
static unsigned char sram[64 * 1024];

/* Hardware microsecond counter, 31 bits (pico_ioctl.h ADVAL -9). */
static unsigned long usec(void)
{
    int sel = -9;
    return (unsigned long)ioctl(fd, PICOIOC_ADVAL, &sel);
}

static unsigned long timed_move(unsigned char *dst, unsigned char *src,
                                unsigned long n)
{
    unsigned long t0 = usec();
    memmove(dst, src, n);
    return usec() - t0;
}

static void row(const char *tag, unsigned long n, unsigned long us)
{
    /* bytes per microsecond == MB/s (1 MB taken as 1e6 bytes) */
    printf("  %-24s %6lu KB  %7lu us  %3lu MB/s\n",
           tag, n / 1024, us, us ? n / us : 0);
}

int main(void)
{
    struct psram_req rq;
    struct psram_stat st;
    unsigned char *ps;
    unsigned long sizes[] = { 16, 32, 64, 128, 256, 512 };
    unsigned long want, n, us, i;

    fd = open("/dev/sys", O_RDWR);
    if (fd < 0) {
        perror("/dev/sys");
        return 1;
    }

    if (ioctl(fd, PSRAMIOC_STAT, &st) < 0) {
        printf("no arena\n");
        return 1;
    }
    printf("arena: total %lu KB, free %lu KB, largest %lu KB\n",
           (unsigned long)st.total / 1024, (unsigned long)st.free / 1024,
           (unsigned long)st.largest / 1024);

    /* Take the largest block we can, capped at 640K: enough to move
     * 512K within it and still have somewhere to move it to. */
    want = st.largest;
    if (want > 640UL * 1024)
        want = 640UL * 1024;
    rq.len = want;
    if (ioctl(fd, PSRAMIOC_ALLOC, &rq) < 0) {
        printf("ALLOC %lu failed\n", want);
        return 1;
    }
    ps = (unsigned char *)rq.base;
    printf("got %lu KB at %p\n\n", want / 1024, ps);

    memset(ps, 'x', want);
    memset(sram, 'x', sizeof(sram));

    printf("PSRAM, editor's case - memmove(p+1, p, n):\n");
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        n = sizes[i] * 1024;
        if (n + 1 > want)
            break;
        us = timed_move(ps + 1, ps, n);
        row("insert one char", n, us);
    }

    printf("\nPSRAM, aligned - memmove(p+4096, p, n):\n");
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        n = sizes[i] * 1024;
        if (n + 4096 > want)
            break;
        us = timed_move(ps + 4096, ps, n);
        row("aligned move", n, us);
    }

    printf("\nSRAM baseline, same one-byte offset:\n");
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        n = sizes[i] * 1024;
        if (n + 1 > sizeof(sram))
            break;
        us = timed_move(sram + 1, sram, n);
        row("insert one char", n, us);
    }

    /* Sustained typing: 200 inserts, each moving a 64K tail.  This is
     * what holding a key down at the top of a 64K file costs. */
    printf("\nsustained: 200 inserts each moving a 64K tail\n");
    {
        unsigned long t0 = usec();
        for (i = 0; i < 200; i++)
            memmove(ps + 1, ps, 64UL * 1024);
        us = usec() - t0;
        printf("  total %lu ms, %lu us per keystroke\n", us / 1000, us / 200);
    }

    ioctl(fd, PSRAMIOC_FREE, &rq.base);
    close(fd);
    return 0;
}

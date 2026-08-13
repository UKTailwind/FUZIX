/* blitbench - Phase 0 spike for PLAN-games.md (mmb2c BLIT/SPRITE).
 *
 * Answers two questions before any engine code is written:
 *
 *  1. What does a per-row GFXIOC_BLIT / GFXIOC_BLITRD actually cost?
 *     The sprite engine wants to move rectangles as one ioctl per row
 *     (16 bytes for a 32px sprite row in mode 7); if that is too slow
 *     the plan's contingency is a rectangle ioctl, and the decision
 *     should be made on this number, not on taste.
 *
 *  2. Does GFXIOC_BLIT accept an arena (PSRAM) source address?
 *     valaddr was taught about owned arenas for cc2; BLIT FLASH wants
 *     to blit straight out of an arena-backed pseudo flash slot, so
 *     prove the gfx path agrees before building on it.
 *
 * Also times GFXIOC_SCROLL for scale - SCROLL2 will be the same
 * memmove with a rotate, so today's scroll bounds tomorrow's budget.
 *
 * Runs in mode 7 (mmb2c MODE 2), restores the console, prints a
 * verdict against the plan's gate: a projected 8-sprite SHOW SAFE
 * frame at or under 2.5 ms.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "../pico_ioctl.h"

#define STRIDE 160
#define HEIGHT 240
#define ROWS_PER_TEST 2000

static int fd;
static uint8_t sram[STRIDE];
static uint8_t sram2[STRIDE];

static unsigned long usec(void)
{
    int sel = -9;
    return (unsigned long)ioctl(fd, PICOIOC_ADVAL, &sel);
}

/* one BLIT or BLITRD of len bytes at a walking row offset */
static unsigned long rowloop(int op, void *buf, int len, int n)
{
    struct gfx_blit gb;
    unsigned long t0;
    int i, y = 0;

    t0 = usec();
    for (i = 0; i < n; i++) {
        gb.offset = (uint16_t)(y * STRIDE);
        gb.len = (uint16_t)len;
        gb.buf = buf;
        if (ioctl(fd, op, &gb)) {
            perror(op == GFXIOC_BLIT ? "BLIT" : "BLITRD");
            return 0;
        }
        if (++y >= HEIGHT)
            y = 0;
    }
    return usec() - t0;
}

static void report(const char *what, unsigned long us, int n)
{
    printf("  %-24s %7lu us / %d = %4lu us/op\n", what, us, n,
           n ? us / n : 0);
}

int main(void)
{
    unsigned long t, rd16, wr16, rd160, wr160;
    struct psram_req rq;
    struct gfx_blit gb;
    uint8_t *ar;
    int mode, i, bad;

    fd = open("/dev/sys", O_RDWR);
    if (fd < 0) {
        perror("/dev/sys");
        return 1;
    }

    mode = 7;
    if (ioctl(fd, GFXIOC_MODE, &mode)) {
        perror("GFXIOC_MODE 7");
        return 1;
    }

    for (i = 0; i < STRIDE; i++)
        sram[i] = (uint8_t)(i * 5 + 1);

    printf("blitbench: %d ops per test, mode 7 (320x240x4)\n",
           ROWS_PER_TEST);

    rd16 = rowloop(GFXIOC_BLITRD, sram2, 16, ROWS_PER_TEST);
    report("BLITRD 16B (sprite row)", rd16, ROWS_PER_TEST);
    wr16 = rowloop(GFXIOC_BLIT, sram, 16, ROWS_PER_TEST);
    report("BLIT   16B (sprite row)", wr16, ROWS_PER_TEST);
    rd160 = rowloop(GFXIOC_BLITRD, sram2, STRIDE, ROWS_PER_TEST);
    report("BLITRD 160B (full row)", rd160, ROWS_PER_TEST);
    wr160 = rowloop(GFXIOC_BLIT, sram, STRIDE, ROWS_PER_TEST);
    report("BLIT   160B (full row)", wr160, ROWS_PER_TEST);

    /* GFXIOC_SCROLL, for the SCROLL2 budget: 8 rows, black fill */
    t = usec();
    for (i = 0; i < 10; i++)
        ioctl(fd, GFXIOC_SCROLL, (void *)(long)(8 << 24));
    t = usec() - t;
    report("SCROLL 8 rows", t, 10);

    /* SHOW SAFE for one 32x32 sprite = per row: restore(wr16) +
     * save(rd16) + rmw draw(rd16+wr16), 32 rows.  The plan gate is
     * eight of those inside 2.5 ms. */
    t = (wr16 * 2 + rd16 * 2) * 32 / ROWS_PER_TEST;
    printf("projected 32x32 SHOW SAFE: %lu us/sprite, x8 = %lu us  [gate 2500]\n",
           t, t * 8);

    /* ---- arena as a BLIT source ---------------------------------- */
    rq.len = 64 * 1024;
    if (ioctl(fd, PSRAMIOC_ALLOC, &rq) < 0) {
        printf("arena: ALLOC failed\n");
        goto done;
    }
    ar = (uint8_t *)rq.base;
    for (i = 0; i < STRIDE; i++)
        ar[i] = (uint8_t)(0x40 + (i & 15));

    gb.offset = 100 * STRIDE;
    gb.len = STRIDE;
    gb.buf = ar;
    if (ioctl(fd, GFXIOC_BLIT, &gb)) {
        perror("arena BLIT");
        printf("arena: BLIT from arena REJECTED\n");
    } else {
        gb.buf = sram2;
        memset(sram2, 0, STRIDE);
        if (ioctl(fd, GFXIOC_BLITRD, &gb))
            perror("arena BLITRD back");
        bad = 0;
        for (i = 0; i < STRIDE; i++)
            if (sram2[i] != ar[i])
                bad++;
        printf("arena: BLIT from arena %s (%d bad bytes)\n",
               bad ? "MISCOMPARED" : "verified", bad);

        t = rowloop(GFXIOC_BLIT, ar, STRIDE, ROWS_PER_TEST);
        report("BLIT 160B arena src", t, ROWS_PER_TEST);
    }
    ioctl(fd, PSRAMIOC_FREE, &rq.base);

done:
    mode = 0xFF;
    ioctl(fd, GFXIOC_MODE, &mode);
    printf("console restored\n");
    close(fd);
    return 0;
}

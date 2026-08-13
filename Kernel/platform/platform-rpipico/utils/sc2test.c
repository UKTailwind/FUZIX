/* sc2test - GFXIOC_SCROLL2 proven by readback, before anything is
 * built on it (PLAN-games Phase 4).
 *
 * Mode 7: paint pixel (x,y) = f(x,y), scroll with wrap, read rows back
 * and check every pixel against f((x-dx) mod w, (y+dy) mod h) - dy>0
 * is picture-up, so the pixel now at y came from y+dy.  Then a fill
 * scroll and a leave scroll, checked at the bands.  1bpp gets the same
 * wrap check in the console mode's geometry.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "../pico_ioctl.h"

static int fd;
static uint8_t row[164];

static int getpx4(int x, int y)
{
    struct gfx_blit gb;

    gb.offset = (uint16_t)(y * 160 + (x >> 1));
    gb.len = 1;
    gb.buf = row;
    if (ioctl(fd, GFXIOC_BLITRD, &gb))
        return -1;
    return (x & 1) ? (row[0] & 15) : (row[0] >> 4);
}

static int f(int x, int y) { return ((x * 3 + y * 5 + (x >> 3)) & 15); }

int main(void)
{
    struct gfx_scroll2 s2;
    struct gfx_blit gb;
    int mode = 7, x, y, bad = 0, i;

    fd = open("/dev/sys", O_RDWR);
    if (fd < 0) {
        perror("/dev/sys");
        return 1;
    }
    if (ioctl(fd, GFXIOC_MODE, &mode)) {
        perror("MODE 7");
        return 1;
    }

    /* paint by rows */
    for (y = 0; y < 240; y++) {
        for (x = 0; x < 320; x += 2)
            row[x >> 1] = (uint8_t)((f(x, y) << 4) | f(x + 1, y));
        gb.offset = (uint16_t)(y * 160);
        gb.len = 160;
        gb.buf = row;
        ioctl(fd, GFXIOC_BLIT, &gb);
    }

    /* wrap scroll: right 3, up 2 */
    s2.dx = 3;
    s2.dy = 2;
    s2.fill = -2;
    if (ioctl(fd, GFXIOC_SCROLL2, &s2)) {
        perror("SCROLL2");
        goto done;
    }
    for (i = 0; i < 200; i++) {
        int sx, sy, want, got;

        x = (i * 7) % 320;
        y = (i * 11) % 240;
        sx = (x - 3 + 320) % 320;
        sy = (y + 2) % 240;
        want = f(sx, sy);
        got = getpx4(x, y);
        if (got != want && bad++ < 5)
            printf("wrap: (%d,%d) want %d got %d\n", x, y, want, got);
    }
    printf("wrap : %d bad\n", bad);

    /* fill scroll: left 5 fills the right band with colour 9 */
    s2.dx = -5;
    s2.dy = 0;
    s2.fill = 0xFF00FF;         /* magenta - index 9 in RGB121 */
    ioctl(fd, GFXIOC_SCROLL2, &s2);
    bad = 0;
    for (y = 0; y < 240; y += 17)
        for (x = 315; x < 320; x++)
            if (getpx4(x, y) != 9)
                bad++;
    printf("fill : %d bad\n", bad);

    /* leave scroll: down 4 leaves the top band as it was */
    for (y = 0; y < 4; y++) {
        for (x = 0; x < 320; x += 2)
            row[x >> 1] = (uint8_t)((f(x, y) << 4) | f(x + 1, y));
        gb.offset = (uint16_t)(y * 160);
        gb.len = 160;
        gb.buf = row;
        ioctl(fd, GFXIOC_BLIT, &gb);
    }
    s2.dx = 0;
    s2.dy = -4;
    s2.fill = -1;
    ioctl(fd, GFXIOC_SCROLL2, &s2);
    bad = 0;
    for (x = 0; x < 320; x += 13)
        for (y = 0; y < 4; y++)
            if (getpx4(x, y) != f(x, y))
                bad++;
    printf("leave: %d bad\n", bad);

done:
    mode = 0xFF;
    ioctl(fd, GFXIOC_MODE, &mode);
    close(fd);
    printf("sc2test done\n");
    return 0;
}

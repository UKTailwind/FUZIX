/* bmtest - GFXIOC_BITMAP and GFXIOC_RECT, checked against readback.
 *
 * The point of reading every pixel back with GFXIOC_GETPIXEL rather than
 * looking at the screen is that bit order is exactly the kind of bug a
 * glance does not catch: a bitmap drawn mirrored within each byte still
 * looks like a bitmap.  Each case draws a pattern whose correct result
 * is known, then asserts on the whole rectangle it should have touched.
 *
 * It runs in a GRAPHICS mode, not on the console: the console owns the
 * same framebuffer and paints its own text and banner into it, so an
 * assertion about "nothing else is lit on this line" is meaningless
 * there.  Mode 7 first (4bpp, and it shares the console's raster so the
 * monitor keeps its lock), then mode 0 (1bpp) which is the depth the
 * editor will draw text in.  The console is always restored.
 *
 *   bmtest          run the assertions, print pass/fail
 *   bmtest -s       leave a row of glyphs on the screen to be looked at
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "../pico_ioctl.h"

static int fd;
static struct gfx_info gi;
static int fails, checks;

#define WHITE 0xFFFFFFL
#define BLACK 0x000000L

/* An 8x8 glyph, MSB first, one byte per row - a diagonal with a marked
 * top-left pixel, so a mirrored byte and a transposed image are both
 * obvious in the readback. */
static unsigned char diag[8] = {
    0xC0,       /* ##...... */
    0x40,       /* .#...... */
    0x20,       /* ..#..... */
    0x10,       /* ...#.... */
    0x08,       /* ....#... */
    0x04,       /* .....#.. */
    0x02,       /* ......#. */
    0x01        /* .......# */
};

static int px(int x, int y)
{
    return ioctl(fd, GFXIOC_GETPIXEL, (void *)GFX_PIXEL_PACK(x, y));
}

static void check(const char *what, int cond)
{
    checks++;
    if (!cond) {
        fails++;
        printf("FAIL %s\n", what);
    }
}

/* Every pixel of the 8x8 glyph, scaled, against the source rows. */
static void verify(const char *what, int x0, int y0, int scale, int lit)
{
    int r, c, i, j, bad = 0;

    for (r = 0; r < 8; r++)
        for (c = 0; c < 8; c++) {
            int want = (diag[r] >> (7 - c)) & 1;
            for (i = 0; i < scale; i++)
                for (j = 0; j < scale; j++) {
                    int on = (px(x0 + c * scale + j, y0 + r * scale + i) > 0);
                    if (!want && !lit)  /* transparent: paper untouched */
                        continue;
                    if (on != want)
                        bad++;
                }
        }
    if (bad)
        printf("  %s: %d of %d pixels wrong\n", what, bad,
               64 * scale * scale);
    check(what, bad == 0);
}

static void bitmap(int x, int y, int scale, long fg, long bg)
{
    struct gfx_bitmap gb;

    gb.x = x;
    gb.y = y;
    gb.width = 8;
    gb.height = 8;
    gb.scale = scale;
    gb.pad = 0;
    gb.fg = fg;
    gb.bg = bg;
    gb.bits = diag;
    if (ioctl(fd, GFXIOC_BITMAP, &gb) < 0)
        perror("GFXIOC_BITMAP");
}

static void fill(int x1, int y1, int x2, int y2, long c)
{
    struct gfx_rect gr;

    gr.x1 = x1; gr.y1 = y1; gr.x2 = x2; gr.y2 = y2;
    ioctl(fd, GFXIOC_COLOUR, (void *)c);
    if (ioctl(fd, GFXIOC_RECT, &gr) < 0)
        perror("GFXIOC_RECT");
}

/* The whole suite, in whatever mode is live.  Nothing here assumes a
 * depth: colours go in as RGB888 and come back as RGB888, which is the
 * whole point of the contract. */
static void suite(void)
{
    int i, w = gi.width, h = gi.height;

    /* 1. the rectangle primitive, which everything else clears with */
    fill(0, 0, w - 1, h - 1, BLACK);
    check("screen starts clear", px(0, 0) == 0 && px(w - 1, h - 1) == 0);
    fill(0, 0, 63, 63, WHITE);
    check("rect fills", px(0, 0) > 0 && px(63, 63) > 0 && px(32, 32) > 0);
    check("rect stops at its edge", px(64, 64) == 0 && px(0, 64) == 0);
    fill(0, 0, 63, 63, BLACK);
    check("rect clears", px(0, 0) == 0 && px(63, 63) == 0);

    /* 2. the glyph at 1:1 */
    bitmap(0, 0, 1, WHITE, BLACK);
    verify("bitmap 1:1", 0, 0, 1, 1);

    /* 3. scaled, which multiplies both axes independently - a
     *    transposed image passes at 1:1 and fails here */
    fill(0, 0, 63, 63, BLACK);
    bitmap(0, 0, 4, WHITE, BLACK);
    verify("bitmap scale 4", 0, 0, 4, 1);

    /* 4. an ODD x, which is where the 4bpp nibble halves diverge */
    fill(0, 0, 63, 63, BLACK);
    bitmap(3, 20, 1, WHITE, BLACK);
    verify("bitmap at odd x", 3, 20, 1, 1);

    /* 5. transparency: paper set first, then only the ink must land */
    fill(0, 0, 63, 63, WHITE);
    fill(0, 0, 7, 7, BLACK);
    bitmap(0, 0, 1, WHITE, -1);
    verify("bitmap transparent", 0, 0, 1, 0);
    check("transparent leaves paper", px(7, 0) == 0);

    /* 6. clipped off the left edge - the half that lands must still be
     *    in the right place, and nothing may wrap onto the line above */
    fill(0, 0, w - 1, h - 1, BLACK);
    bitmap(-4, 8, 1, WHITE, BLACK);
    check("clip: visible half correct", px(0, 12) > 0);
    for (i = 0; i < w; i++)
        if (px(i, 7) > 0)
            break;
    check("clip: no wrap onto the line above", i == w);

    /* 7. wholly off-screen must not draw or fault */
    fill(0, 0, w - 1, h - 1, BLACK);
    bitmap(-100, -100, 1, WHITE, BLACK);
    bitmap(w + 10, 10, 1, WHITE, BLACK);
    bitmap(10, h + 10, 1, WHITE, BLACK);
    for (i = 0; i < w; i++)
        if (px(i, 0) > 0 || px(i, 10) > 0)
            break;
    check("off-screen is a no-op", i == w);
}

static int setmode(int m)
{
    if (ioctl(fd, GFXIOC_MODE, &m) < 0) {
        perror("GFXIOC_MODE");
        return -1;
    }
    if (ioctl(fd, GFXIOC_INFO, &gi) < 0) {
        perror("GFXIOC_INFO");
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int show = (argc > 1 && strcmp(argv[1], "-s") == 0);
    int console = 0xFF;
    int i;

    fd = open("/dev/sys", O_RDWR);
    if (fd < 0) {
        perror("/dev/sys");
        return 1;
    }

    for (i = 0; i < 2; i++) {
        int mode = i ? 0 : 7;
        if (setmode(mode) < 0)
            break;
        printf("mode %d: %dx%d %dbpp\n", gi.mode, gi.width, gi.height,
               gi.bpp);
        suite();
    }

    if (show) {
        setmode(7);
        fill(0, 0, gi.width - 1, gi.height - 1, BLACK);
        for (i = 0; i < 8; i++)
            bitmap(i * 36, 20, 4, 0xFF0000L + (long)i * 0x2000L, -1);
        sleep(3);
    }

    ioctl(fd, GFXIOC_MODE, &console);
    printf("bmtest: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}

/* gfxtest - test card and mode-switch check for the PC3 graphics modes.
 *
 *   gfxtest [mode ...]     (default 1)
 *
 * Draws a test card in each mode given, in turn, advancing on Enter,
 * then restores the console.  Give several modes to watch what the
 * monitor does at each switch: modes 0-5 share the 1024x768 raster and
 * the console and mode 7 share 640x480, so a switch inside either group
 * should change the picture without the monitor resyncing, and only
 * crossing between the groups should drop the signal.  For example
 *
 *   gfxtest 7 1 2 0 7
 *
 * should resync exactly twice - 7->1 and 0->7 - and not at 1->2, 2->0.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "../pico_ioctl.h"

static uint8_t fb[160 * 256];           /* biggest mode framebuffer */
static int stride, width, height, bpp, ncol;

static void pset(int x, int y, int c)
{
    uint8_t *p;
    if (x < 0 || y < 0 || x >= width || y >= height)
        return;
    if (bpp == 4) {
        p = fb + y * stride + (x >> 1);
        if (x & 1)
            *p = (*p & 0xF0) | (c & 15);
        else
            *p = (*p & 0x0F) | (c << 4);
    } else {
        p = fb + y * stride + (x >> 3);
        if (c)
            *p |= 0x80 >> (x & 7);
        else
            *p &= ~(0x80 >> (x & 7));
    }
}

static void box(int x0, int y0, int x1, int y1, int c)
{
    int x, y;
    for (x = x0; x <= x1; x++) {
        pset(x, y0, c);
        pset(x, y1, c);
    }
    for (y = y0; y <= y1; y++) {
        pset(x0, y, c);
        pset(x1, y, c);
    }
}

/* Push the whole framebuffer.  gfx_blit carries a uint16_t length, so
 * a mode bigger than 64K would need splitting; none is, but the loop
 * below keeps each transfer to 20K anyway. */
static void push(int fd)
{
    struct gfx_blit gb;
    int off, size = stride * height;

    for (off = 0; off < size; off += 20480) {
        gb.offset = off;
        gb.len = (size - off > 20480) ? 20480 : (size - off);
        gb.buf = fb + off;
        if (ioctl(fd, GFXIOC_BLIT, &gb))
            perror("GFXIOC_BLIT");
    }
}

static int geometry(int mode)
{
    switch (mode) {
    case 0: case 3:
        stride = 80;  width = 640; height = 256; bpp = 1; ncol = 2;
        return 0;
    case 1: case 4:
        stride = 160; width = 320; height = 256; bpp = 4; ncol = 4;
        return 0;
    case 2: case 5:
        stride = 80;  width = 160; height = 256; bpp = 4; ncol = 16;
        return 0;
    case 7:
        stride = 160; width = 320; height = 240; bpp = 4; ncol = 16;
        return 0;
    }
    return -1;
}

static void testcard(int fd, int mode)
{
    int i, x, y, bars = height * 3 / 4;

    memset(fb, 0, sizeof(fb));

    if (bpp == 4) {
        /* One logical colour per physical colour: modes 0-5 only reach
         * the 8 authentic BBC colours, mode 7 reaches all 16. */
        for (i = 0; i < 16; i++) {
            int v = (i << 8) | ((mode == 7) ? i : (i & 7));
            ioctl(fd, GFXIOC_PAL, &v);
        }
        /* colour bars across the full palette */
        for (y = 0; y < bars; y++)
            for (x = 0; x < width; x++)
                pset(x, y, (x * 16) / width);
        /* two-tone strip below, to show the palette really took */
        for (y = bars + 8; y < height - 8; y++)
            for (x = 0; x < width; x++)
                pset(x, y, (x & 8) ? 7 : 4);
    } else {
        /* checkerboard + diagonals */
        for (y = 0; y < height; y++)
            for (x = 0; x < width; x++)
                if (((x >> 4) ^ (y >> 4)) & 1)
                    pset(x, y, 1);
        for (x = 0; x < width; x++) {
            pset(x, (x * height) / width, 1);
            pset(x, height - 1 - (x * height) / width, 0);
        }
    }

    /* geometry check: single-pixel border and centre cross.  If the
     * border is clipped or doubled, the expander's scaling is wrong. */
    box(0, 0, width - 1, height - 1, bpp == 4 ? 7 : 1);
    for (x = 0; x < width; x++)
        pset(x, height / 2, bpp == 4 ? 7 : 1);
    for (y = 0; y < height; y++)
        pset(width / 2, y, bpp == 4 ? 7 : 1);

    push(fd);
}

static void wait_enter(void)
{
    char c;
    while (read(0, &c, 1) == 1 && c != '\n' && c != '\r')
        ;
}

int main(int argc, char *argv[])
{
    int fd, mode, arg, last = (argc > 1) ? argc - 1 : 1;

    fd = open("/dev/sys", O_RDONLY);
    if (fd < 0) {
        perror("/dev/sys");
        return 1;
    }

    for (arg = 1; arg <= last; arg++) {
        mode = (argc > 1) ? atoi(argv[arg]) : 1;
        if (geometry(mode)) {
            fprintf(stderr, "gfxtest: no mode %d (0-5, 7)\n", mode);
            continue;
        }

        printf("MODE %d: %dx%d, %d colours. Enter for the next.\n",
               mode, width, height, ncol);
        fflush(stdout);

        if (ioctl(fd, GFXIOC_MODE, &mode)) {
            perror("GFXIOC_MODE");
            break;
        }
        testcard(fd, mode);
        wait_enter();
    }

    mode = 0xFF;
    ioctl(fd, GFXIOC_MODE, &mode);
    close(fd);
    printf("console restored\n");
    return 0;
}

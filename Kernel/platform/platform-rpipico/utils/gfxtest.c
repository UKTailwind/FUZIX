/* gfxtest - first light for the PC3 BBC graphics modes.
 *
 *   gfxtest [mode]     (default 1)
 *
 * Enters the mode, draws a test card from userland through the
 * GFXIOC interface, waits for Enter, restores the console.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "../pico_ioctl.h"

static uint8_t fb[160 * 256];           /* biggest mode framebuffer */
static int stride, width, bpp;

static void pset(int x, int y, int c)
{
    uint8_t *p;
    if (x < 0 || y < 0 || x >= width || y >= 256)
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

int main(int argc, char *argv[])
{
    int fd, mode = 1, i, x, y;
    struct gfx_blit gb;

    if (argc > 1)
        mode = atoi(argv[1]);

    fd = open("/dev/sys", O_RDONLY);
    if (fd < 0) {
        perror("/dev/sys");
        return 1;
    }

    switch (mode) {
    case 0: case 3:
        stride = 80; width = 640; bpp = 1;
        break;
    case 1: case 4:
        stride = 160; width = 320; bpp = 4;
        break;
    case 2: case 5:
        stride = 80; width = 160; bpp = 4;
        break;
    default:
        fprintf(stderr, "gfxtest: mode 0-5\n");
        return 1;
    }

    printf("MODE %d: %dx256, %d colours. Enter to exit.\n",
           mode, width, bpp == 4 ? 16 : 2);

    if (ioctl(fd, GFXIOC_MODE, &mode)) {
        perror("GFXIOC_MODE");
        return 1;
    }

    if (bpp == 4) {
        /* all 16 logical colours: repeat the 8 physicals */
        for (i = 0; i < 16; i++) {
            int v = (i << 8) | (i & 7);
            ioctl(fd, GFXIOC_PAL, &v);
        }
        /* colour bars */
        for (y = 0; y < 200; y++)
            for (x = 0; x < width; x++)
                pset(x, y, (x * 16) / width);
        /* greyscale-ish strip of the darker set below */
        for (y = 208; y < 248; y++)
            for (x = 0; x < width; x++)
                pset(x, y, (x & 8) ? 7 : 4);
    } else {
        /* checkerboard + diagonals */
        for (y = 0; y < 256; y++)
            for (x = 0; x < width; x++)
                if (((x >> 4) ^ (y >> 4)) & 1)
                    pset(x, y, 1);
        for (x = 0; x < width; x++) {
            pset(x, (x * 256) / width, 1);
            pset(x, 255 - (x * 256) / width, 0);
        }
    }

    /* geometry check: single-pixel border and centre cross */
    box(0, 0, width - 1, 255, bpp == 4 ? 7 : 1);
    for (x = 0; x < width; x++)
        pset(x, 128, bpp == 4 ? 7 : 1);
    for (y = 0; y < 256; y++)
        pset(width / 2, y, bpp == 4 ? 7 : 1);

    gb.offset = 0;
    gb.len = 20480;
    gb.buf = fb;
    if (ioctl(fd, GFXIOC_BLIT, &gb))
        perror("GFXIOC_BLIT");
    if (stride == 160) {
        gb.offset = 20480;
        gb.buf = fb + 20480;
        if (ioctl(fd, GFXIOC_BLIT, &gb))
            perror("GFXIOC_BLIT");
    }

    /* wait for Enter (serial or keyboard) */
    {
        char c;
        while (read(0, &c, 1) == 1 && c != '\n' && c != '\r')
            ;
    }

    mode = 0xFF;
    ioctl(fd, GFXIOC_MODE, &mode);
    close(fd);
    printf("console restored\n");
    return 0;
}

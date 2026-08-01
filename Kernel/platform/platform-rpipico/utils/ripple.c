/* ripple - the hidden-line ripple surface, hand-translated from the
 * MMBasic demo, to test and time the kernel's pixel primitive.
 *
 *   Timer =0 : CLS
 *   xs=0.5 : ys=0.5 : a=159 : b=a*a : c=160
 *   For x=0 To a Step xs
 *     s=x*x : p=Sqr(b-s)
 *     For i=-p To p Step 6*ys
 *       r=Sqr(s+i*i)/a
 *       q=(r-1)*Sin(24*r)
 *       y=Int(i/3+q*c)
 *       If i=-p Then m=y : n=y
 *       If y>m Then m=y
 *       If y<n Then n=y
 *       If (m=y) Or (n=y) Then
 *         Pixel 160-x,160-y
 *         Pixel 160+x,160-y
 *       EndIf
 *     Next i
 *   Next x
 *   Print Timer
 *
 * This is what mmbc should generate for it once PIXEL exists, so the
 * time is directly comparable with MMBasic on the same board.
 *
 *   ripple          draw through GFXIOC_PIXEL, one syscall per pixel
 *   ripple -b       draw into a shadow buffer, one GFXIOC_BLIT at the end
 *   ripple -n       compute everything, draw nothing (the arithmetic alone)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "../pico_ioctl.h"

static int fd;
static struct gfx_info gi;
static unsigned char shadow[640 / 8 * 480];
static int mode_buf, mode_none;
static unsigned long npix;

static unsigned long usec(void)
{
    int sel = -9;
    return (unsigned long)ioctl(fd, PICOIOC_ADVAL, &sel);
}

static void plot(int x, int y)
{
    npix++;
    if (mode_none)
        return;
    if (mode_buf) {
        if (x < 0 || y < 0 || x >= gi.width || y >= gi.height)
            return;
        shadow[y * gi.stride + (x >> 3)] |= 0x80 >> (x & 7);
        return;
    }
    ioctl(fd, GFXIOC_PIXEL, (void *)GFX_PIXEL_PACK(x, y, 1));
}

int main(int argc, char *argv[])
{
    double xs = 0.5, ys = 0.5, a = 159.0, b, c = 160.0;
    double x, s, p, i, r, q;
    int y, m = 0, n = 0;
    unsigned long t0, us;

    if (argc > 1 && strcmp(argv[1], "-b") == 0)
        mode_buf = 1;
    if (argc > 1 && strcmp(argv[1], "-n") == 0)
        mode_none = 1;

    fd = open("/dev/sys", O_RDWR);
    if (fd < 0) {
        perror("/dev/sys");
        return 1;
    }
    if (ioctl(fd, GFXIOC_INFO, &gi)) {
        perror("GFXIOC_INFO");
        return 1;
    }
    printf("mode %02X: %dx%d, %d bpp, stride %d\n",
           gi.mode, gi.width, gi.height, gi.bpp, gi.stride);
    if (gi.bpp != 1) {
        printf("this demo wants the 640x480 1bpp console\n");
        return 1;
    }
    memset(shadow, 0, sizeof(shadow));

    b = a * a;
    t0 = usec();

    for (x = 0.0; x <= a; x += xs) {
        s = x * x;
        p = sqrt(b - s);
        for (i = -p; i <= p; i += 6.0 * ys) {
            r = sqrt(s + i * i) / a;
            q = (r - 1.0) * sin(24.0 * r);
            y = (int)(i / 3.0 + q * c);
            if (i == -p) {
                m = y;
                n = y;
            }
            if (y > m) m = y;
            if (y < n) n = y;
            if (m == y || n == y) {
                plot((int)(160.0 - x), 160 - y);
                plot((int)(160.0 + x), 160 - y);
            }
        }
    }

    if (mode_buf) {
        struct gfx_blit gb;
        int off, size = gi.stride * gi.height;
        for (off = 0; off < size; off += 20480) {
            gb.offset = off;
            gb.len = (size - off > 20480) ? 20480 : (size - off);
            gb.buf = shadow + off;
            if (ioctl(fd, GFXIOC_BLIT, &gb))
                perror("GFXIOC_BLIT");
        }
    }

    us = usec() - t0;
    printf("%s: %lu pixels in %lu us (%lu ms)\n",
           mode_none ? "compute only" : mode_buf ? "shadow+blit" : "ioctl per pixel",
           npix, us, us / 1000);
    if (npix)
        printf("  %lu ns per plotted pixel (all costs included)\n",
               (us * 1000UL) / npix);
    close(fd);
    return 0;
}

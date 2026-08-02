/* linebench - is one batched ioctl really worth it, and does it draw
 * the same thing?
 *
 * The claim being tested: the syscall costs 1.3us and a pixel store
 * costs 15ns, so a line drawn as N separate GFXIOC_PIXEL calls is
 * dominated by the crossing, and handing the kernel the whole run in
 * one GFXIOC_PIXELS should be an order of magnitude better.  The
 * estimate that justified building it was ~17us for 300 points against
 * ~390us; this says whether that was right.
 *
 * It runs in mode 7 rather than on the console, so readback means what
 * it says - on the console the text and the pixels share a framebuffer
 * and any printf moves what you are trying to measure.
 *
 *   linebench            time both paths, then check they agree
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "../pico_ioctl.h"

#define MAXPT 1024

static int fd;
static struct gfx_info gi;
static struct gfx_pt pts[MAXPT];

static unsigned long usec(void)
{
    int sel = -9;
    return (unsigned long)ioctl(fd, PICOIOC_ADVAL, &sel);
}

/* Bresenham, in userland - this is the whole point: the geometry lives
 * out here where it costs no kernel memory, and only the run of points
 * crosses into the kernel. */
static int line_points(int x1, int y1, int x2, int y2, struct gfx_pt *out)
{
    int dx = x2 - x1, dy = y2 - y1;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    int err, n = 0;

    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    err = (dx > dy ? dx : -dy) / 2;

    for (;;) {
        if (n < MAXPT) {
            out[n].x = (short)x1;
            out[n].y = (short)y1;
            n++;
        }
        if (x1 == x2 && y1 == y2)
            break;
        {
            int e2 = err;
            if (e2 > -dx) { err -= dy; x1 += sx; }
            if (e2 < dy)  { err += dx; y1 += sy; }
        }
    }
    return n;
}

static void draw_one_by_one(const struct gfx_pt *p, int n)
{
    int i;
    for (i = 0; i < n; i++)
        ioctl(fd, GFXIOC_PIXEL, (void *)GFX_PIXEL_PACK(p[i].x, p[i].y));
}

static void draw_batched(const struct gfx_pt *p, int n)
{
    struct gfx_batch b;

    b.count = (unsigned short)n;
    b.flags = 0;
    b.items = (void *)p;
    b.colours = 0;
    if (ioctl(fd, GFXIOC_PIXELS, &b) < 0)
        perror("GFXIOC_PIXELS");
}

static int px(int x, int y)
{
    return ioctl(fd, GFXIOC_GETPIXEL, (void *)GFX_PIXEL_PACK(x, y));
}

/* Clear, then put the drawing colour BACK.  GFXIOC_COLOUR sets the one
 * current colour that every primitive draws in, so a clear to black
 * leaves it black - and the first version of this test then measured
 * both paths drawing black on black and reported every point missing. */
static void fill(long c)
{
    struct gfx_rect r;

    r.x1 = 0; r.y1 = 0;
    r.x2 = (short)(gi.width - 1); r.y2 = (short)(gi.height - 1);
    ioctl(fd, GFXIOC_COLOUR, (void *)c);
    ioctl(fd, GFXIOC_RECT, &r);
    ioctl(fd, GFXIOC_COLOUR, (void *)0xFFFFFFL);
}

int main(void)
{
    int mode = 7, console = 0xFF;
    unsigned long t0, t1, t2;
    int n, i, reps = 20, bad = 0;

    fd = open("/dev/sys", O_RDWR);
    if (fd < 0) {
        perror("/dev/sys");
        return 1;
    }
    if (ioctl(fd, GFXIOC_MODE, &mode) < 0) {
        perror("GFXIOC_MODE");
        return 1;
    }
    ioctl(fd, GFXIOC_INFO, &gi);
    printf("mode %d: %dx%d %dbpp\n", gi.mode, gi.width, gi.height, gi.bpp);

    /* a long diagonal, the shape the estimate was about */
    n = line_points(4, 4, gi.width - 5, gi.height - 5, pts);
    printf("line of %d points, %d reps\n", n, reps);

    ioctl(fd, GFXIOC_COLOUR, (void *)0xFFFFFFL);

    fill(0);
    t0 = usec();
    for (i = 0; i < reps; i++)
        draw_one_by_one(pts, n);
    t1 = usec();

    fill(0);
    for (i = 0; i < reps; i++)
        draw_batched(pts, n);
    t2 = usec();

    printf("one ioctl per point : %lu us total, %lu us per line\n",
           t1 - t0, (t1 - t0) / reps);
    printf("one ioctl per line  : %lu us total, %lu us per line\n",
           t2 - t1, (t2 - t1) / reps);
    if (t2 - t1)
        printf("batching is %lu times faster\n", (t1 - t0) / (t2 - t1));

    /*
     * Where does the per-point cost actually go?  The batched loop is
     * maybe twenty instructions, which at 378MHz should be nothing like
     * the measured 223ns, and compiling it -O2 changed nothing - so it
     * is not instruction bound.  The obvious suspect is memory: a
     * DIAGONAL touches a different framebuffer row for every point,
     * while a HORIZONTAL run of the same length walks consecutive
     * bytes.  Same count, same kernel path, only the locality differs.
     */
    {
        int m = line_points(4, gi.height / 2, 4 + n - 1, gi.height / 2, pts);
        unsigned long h0, h1;

        fill(0);
        h0 = usec();
        for (i = 0; i < reps; i++)
            draw_batched(pts, m);
        h1 = usec();
        printf("horizontal %d points : %lu us per line (%lu ns/point)\n",
               m, (h1 - h0) / reps, ((h1 - h0) * 1000) / (reps * m));
        /* put the diagonal back for the correctness check below */
        n = line_points(4, 4, gi.width - 5, gi.height - 5, pts);
        fill(0);
        for (i = 0; i < reps; i++)
            draw_batched(pts, n);
    }

    /* and it has to draw the SAME thing */
    for (i = 0; i < n; i++)
        if (px(pts[i].x, pts[i].y) <= 0)
            bad++;
    printf("after batching: %d of %d points missing\n", bad, n);

    /* nothing beyond the line should have been touched */
    if (px(2, 2) != 0 || px(gi.width - 2, 2) != 0)
        printf("FAIL: drew outside the line\n");

    ioctl(fd, GFXIOC_MODE, &console);
    return bad ? 1 : 0;
}

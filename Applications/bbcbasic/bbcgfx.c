/* bbcgfx.c - BBC BASIC graphics for the Pico Computer 3 (Fuzix).
 *
 * Implements the graphics VDU stream against the kernel's BBC modes
 * (PC3-GFX-DESIGN.md): the interpreter renders into a shadow
 * framebuffer here and pushes dirty rows through /dev/sys GFXIOC.
 *
 * Modes 0-5 (mode 3 = 0, 4 = 1, 5 = 2 sizes):
 *   0: 640x256, 2 colours, 1bpp, 80 bytes/line
 *   1: 320x256, 4 colours (stored 4bpp), 160 bytes/line
 *   2: 160x256, 16 colours, 4bpp, 80 bytes/line
 * Graphics units are the authentic 1280x1024, origin bottom-left,
 * movable with VDU 29.  Text in a graphics mode renders with the
 * interpreter's own 8x8 font.  MODE with n > 5 (or QUIT) returns to
 * the Fuzix text console.
 *
 * Hooked from xeqvdu (bbccos.c) ahead of the ANSI terminal path:
 * fuzix_gfx_vdu() returns 1 when it consumed the call.  While a
 * graphics mode is active it consumes everything.
 */

#ifdef FUZIX

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

/* Kernel interface (mirrors platform pico_ioctl.h) */
#define GFXIOC_MODE   0x0003
#define GFXIOC_PAL    0x0004
#define GFXIOC_BLIT   0x0005
struct gfx_blit {
    uint16_t offset;
    uint16_t len;
    void *buf;
};

extern unsigned char bbcfont[];         /* 8 bytes per glyph */
extern int ioctl(int, int, ...);

static uint8_t fb[160 * 256];           /* shadow framebuffer (worst case) */
static int fd = -1;
static int curmode = -1;                /* -1 = console */
static int width, stride, bpp, colmask;

/* graphics state, BBC units */
static int gx[3], gy[3];                /* point history: [0] newest */
static int ox, oy;                      /* VDU 29 origin */
static uint8_t gfg, gbg;                /* GCOL colours */
static uint8_t tfg, tbg;                /* COLOUR text colours */
static int tx, ty;                      /* text cursor, character cells */
static int tcols, trows;
static int dirty_lo, dirty_hi;          /* dirty pixel-row range */

int gfx_active(void)
{
    return curmode >= 0;
}

/* --- dirty-row flush ------------------------------------------------------ */
static void mark(int y)
{
    if (y < dirty_lo) dirty_lo = y;
    if (y > dirty_hi) dirty_hi = y;
}

static void flush(void)
{
    struct gfx_blit gb;
    if (dirty_hi < dirty_lo)
        return;
    gb.offset = dirty_lo * stride;
    gb.len = (dirty_hi - dirty_lo + 1) * stride;
    gb.buf = fb + gb.offset;
    if (fd >= 0)
        ioctl(fd, GFXIOC_BLIT, &gb);
    dirty_lo = 256;
    dirty_hi = -1;
}

/* --- pixel primitives (pixel coordinates, y down) ------------------------- */
static void pset(int x, int y, uint8_t c)
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
        if (c & 1)
            *p |= 0x80 >> (x & 7);
        else
            *p &= ~(0x80 >> (x & 7));
    }
    mark(y);
}

static void hline(int x0, int x1, int y, uint8_t c)
{
    int x;
    if (y < 0 || y >= 256)
        return;
    if (x0 > x1) { x = x0; x0 = x1; x1 = x; }
    if (x0 < 0) x0 = 0;
    if (x1 >= width) x1 = width - 1;
    for (x = x0; x <= x1; x++)
        pset(x, y, c);
}

static void line(int x0, int y0, int x1, int y1, uint8_t c)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        pset(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* filled triangle: standard scanline edge-walk with 16.16 slopes */
static void triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t c)
{
    int t, y;
    long xa, xb, da, db, dc;

    if (y0 > y1) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y0 > y2) { t = y0; y0 = y2; y2 = t; t = x0; x0 = x2; x2 = t; }
    if (y1 > y2) { t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }

    if (y2 == y0) {
        hline(x0, x2, y0, c);
        return;
    }
    da = ((long)(x1 - x0) << 16) / (y1 - y0 ? y1 - y0 : 1);
    db = ((long)(x2 - x0) << 16) / (y2 - y0);
    dc = ((long)(x2 - x1) << 16) / (y2 - y1 ? y2 - y1 : 1);

    xa = (long)x0 << 16;
    xb = xa;
    for (y = y0; y < y1; y++) {
        hline((int)(xa >> 16), (int)(xb >> 16), y, c);
        xa += da;
        xb += db;
    }
    xa = (long)x1 << 16;
    for (y = y1; y <= y2; y++) {
        hline((int)(xa >> 16), (int)(xb >> 16), y, c);
        xa += dc;
        xb += db;
    }
}

/* --- BBC units -> pixels -------------------------------------------------- */
static int xshift;      /* units-per-pixel log2: mode0=1, mode1=2, mode2=3 */

static int px_of(int ux) { return (ux + ox) >> xshift; }
static int py_of(int uy) { return 255 - ((uy + oy) >> 2); }

/* --- text rendering ------------------------------------------------------- */
static void putglyph(int ch, int col, int row)
{
    const unsigned char *g = &bbcfont[(ch & 0xFF) << 3];
    int x0 = col * 8, y0 = row * 8, r, b;
    for (r = 0; r < 8; r++) {
        unsigned char bits = g[r];
        for (b = 0; b < 8; b++)
            pset(x0 + b, y0 + r, (bits & (0x80 >> b)) ? tfg : tbg);
    }
}

static void text_scroll(void)
{
    memmove(fb, fb + 8 * stride, (256 - 8) * stride);
    memset(fb + (256 - 8) * stride, 0, 8 * stride);
    dirty_lo = 0;
    dirty_hi = 255;
}

static void text_char(int ch)
{
    switch (ch) {
    case 8:
        if (tx > 0) tx--;
        return;
    case 9:
        if (++tx >= tcols) { tx = 0; goto down; }
        return;
    case 10:
    down:
        if (++ty >= trows) { ty = trows - 1; text_scroll(); }
        return;
    case 11:
        if (ty > 0) ty--;
        return;
    case 13:
        tx = 0;
        return;
    case 30:
        tx = ty = 0;
        return;
    case 12:
        memset(fb, 0, stride * 256);
        dirty_lo = 0; dirty_hi = 255;
        tx = ty = 0;
        return;
    case 127:
        if (tx > 0) tx--;
        putglyph(' ', tx, ty);
        return;
    }
    if (ch < 32)
        return;
    putglyph(ch, tx, ty);
    if (++tx >= tcols) {
        tx = 0;
        if (++ty >= trows) { ty = trows - 1; text_scroll(); }
    }
}

/* --- mode control --------------------------------------------------------- */
static int gfx_enter(int n)
{
    int m;
    if (fd < 0) {
        fd = open("/dev/sys", O_RDONLY);
        if (fd < 0)
            return -1;
    }
    m = n;
    if (ioctl(fd, GFXIOC_MODE, &m))
        return -1;

    switch (n) {
    case 0: case 3:
        width = 640; stride = 80; bpp = 1; colmask = 1; xshift = 1;
        break;
    case 1: case 4:
        width = 320; stride = 160; bpp = 4; colmask = 3; xshift = 2;
        break;
    default: /* 2, 5 */
        width = 160; stride = 80; bpp = 4; colmask = 15; xshift = 3;
        break;
    }
    curmode = n;
    memset(fb, 0, sizeof(fb));
    gx[0] = gx[1] = gx[2] = gy[0] = gy[1] = gy[2] = 0;
    ox = oy = 0;
    gfg = colmask;                      /* white */
    gbg = 0;
    tfg = colmask;
    tbg = 0;
    tx = ty = 0;
    tcols = width / 8;
    trows = 32;
    dirty_lo = 256;
    dirty_hi = -1;
    return 0;
}

static void gfx_exit(void)
{
    int m = 0xFF;
    if (curmode >= 0 && fd >= 0)
        ioctl(fd, GFXIOC_MODE, &m);
    curmode = -1;
}

void gfx_shutdown(void)
{
    gfx_exit();
}

/* --- PLOT ----------------------------------------------------------------- */
static void plot(int k, int x, int y)
{
    int abs_ = k & 4;

    /* shift point history */
    gx[2] = gx[1]; gy[2] = gy[1];
    gx[1] = gx[0]; gy[1] = gy[0];
    if (abs_) {
        gx[0] = x;
        gy[0] = y;
    } else {
        gx[0] = gx[1] + x;
        gy[0] = gy[1] + y;
    }

    if ((k & 3) == 0)
        return;                         /* +0/+4 = move, every family */

    if (k < 64) {
        /* line family: draw previous -> current */
        uint8_t c = ((k & 3) == 3) ? gbg : gfg;
        line(px_of(gx[1]), py_of(gy[1]), px_of(gx[0]), py_of(gy[0]), c);
    } else if (k < 72) {
        pset(px_of(gx[0]), py_of(gy[0]), ((k & 3) == 3) ? gbg : gfg);
    } else if (k >= 80 && k < 88) {
        uint8_t c = ((k & 3) == 3) ? gbg : gfg;
        triangle(px_of(gx[2]), py_of(gy[2]),
                 px_of(gx[1]), py_of(gy[1]),
                 px_of(gx[0]), py_of(gy[0]), c);
    }
    /* other families: position already updated */
}

/* --- VDU dispatch ---------------------------------------------------------
 * Parameter packing (see oswrch/bbccon.c): for an n-parameter VDU the
 * bytes live at vduq[9-n..8] with the VDU number at vduq[9]:
 *   code  = vduq[8] | vduq[9]<<8   (last parameter | vdu<<8)
 *   data1 = vduq[4..7], data2 = vduq[0..3] (little-endian)
 */
int fuzix_gfx_vdu(int code, int data1, int data2)
{
    int vdu = (code >> 8) & 0xFF;

    (void)data2;

    if (curmode < 0) {
        /* console mode: only MODE 0-5 is ours */
        if (vdu == 22 && (code & 0xFF) <= 5) {
            if (gfx_enter(code & 0xFF) == 0)
                return 1;
        }
        return 0;
    }

    switch (vdu) {
    case 22:                            /* MODE */
        if ((code & 0xFF) <= 5) {
            gfx_enter(code & 0xFF);
        } else {
            gfx_exit();
        }
        return 1;

    case 25: {                          /* PLOT k,x;y; */
        int k = data1 & 0xFF;
        int16_t x = (int16_t)((data1 >> 8) & 0xFFFF);
        int16_t y = (int16_t)(((data1 >> 24) & 0xFF) | ((code & 0xFF) << 8));
        plot(k, x, y);
        flush();
        return 1;
    }

    case 16:                            /* CLG */
        memset(fb, (bpp == 4) ? (gbg | (gbg << 4)) :
                   ((gbg & 1) ? 0xFF : 0), stride * 256);
        dirty_lo = 0;
        dirty_hi = 255;
        flush();
        return 1;

    case 17:                            /* COLOUR n */
        if ((code & 0xFF) & 0x80)
            tbg = (code & 0xFF) & colmask;
        else
            tfg = (code & 0xFF) & colmask;
        return 1;

    case 18: {                          /* GCOL m,c */
        int c = code & 0xFF;
        if (c & 0x80)
            gbg = c & colmask;
        else
            gfg = c & colmask;
        return 1;
    }

    case 19: {                          /* VDU 19,l,p,0,0,0 */
        int v = (((data1 & 15) << 8) | ((data1 >> 8) & 15));
        if (fd >= 0)
            ioctl(fd, GFXIOC_PAL, &v);
        return 1;
    }

    case 29: {                          /* graphics origin x;y; */
        ox = (int16_t)((data1 >> 8) & 0xFFFF);
        oy = (int16_t)(((data1 >> 24) & 0xFF) | ((code & 0xFF) << 8));
        return 1;
    }

    case 31: {                          /* TAB(x,y) */
        int x = (data1 >> 24) & 0xFF;
        int y = code & 0xFF;
        if (x < tcols && y < trows) {
            tx = x;
            ty = y;
            printf("\033[%d;%dH", ty + 1, tx + 1);
            fflush(stdout);
        }
        return 1;
    }

    default:
        if (vdu >= 32 || vdu == 8 || vdu == 9 || vdu == 10 ||
            vdu == 11 || vdu == 12 || vdu == 13 || vdu == 30 ||
            vdu == 127) {
            text_char(vdu);
            /* Mirror text to the serial console as a plain stream
             * (the kernel console is suspended in graphics modes, so
             * this write reaches only the uart). */
            if (vdu == 12)
                fputs("\033[2J\033[H", stdout);
            else if (vdu == 30)
                fputs("\033[H", stdout);
            else
                putchar(vdu);
            fflush(stdout);
        }
        flush();
        return 1;                       /* consume everything in-mode */
    }
}

#endif /* FUZIX */

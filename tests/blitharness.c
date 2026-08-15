/* blitharness - pixel-exact checks for mmb_blit.h on the host.
 *
 * The .bas gates run headless and check the command surface; the board
 * checks reality.  This closes the gap between them: a fake framebuffer
 * behind the mm_fb_* window, and every engine operation compared
 * against an independent per-pixel model of the reference semantics.
 * The nibble/bit packing here is written from the PC3 spec (4bpp HIGH
 * nibble = left pixel, 1bpp MSB = left) separately from the engine's
 * code, which is exactly how a mirrored-packing bug gets caught.
 *
 * Build and run:  make blitcheck   (part of make check)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mmb_runtime.h"

/* ---- the fake framebuffer ------------------------------------------- */

static unsigned char fb[160 * 480];
static int cur_stride, cur_bpp, cur_hres, cur_vres;

static void setmode(int m)
{
    if (m == 2) {
        cur_stride = 160; cur_bpp = 4; cur_hres = 320; cur_vres = 240;
    } else {
        cur_stride = 80; cur_bpp = 1; cur_hres = 640; cur_vres = 480;
    }
    memset(fb, 0, sizeof(fb));
}

MMINTEGER mm_hres(void) { return cur_hres; }
MMINTEGER mm_vres(void) { return cur_vres; }
MMINTEGER mm_fb_geom(void) { return (cur_stride << 8) | cur_bpp; }

MMINTEGER mm_fb_read(MMINTEGER offset, MMINTEGER len, void *buf)
{
    if (offset < 0 || offset + len > (MMINTEGER)cur_stride * cur_vres)
        return -1;
    memcpy(buf, fb + offset, (size_t)len);
    return 0;
}

MMINTEGER mm_fb_put(MMINTEGER offset, MMINTEGER len, const void *buf)
{
    if (offset < 0 || offset + len > (MMINTEGER)cur_stride * cur_vres)
        return -1;
    memcpy(fb + offset, buf, (size_t)len);
    return 0;
}

/*
 * The rectangle forms - `rows` rows of `len` bytes, the framebuffer
 * advancing by `stride` between them while the caller's buffer is
 * PACKED at len bytes a row (mmb_blit.h indexes it as base + y*len).
 *
 * These were added to mmb_blit.h with GFXIOC_BLITR and not to this
 * harness, so from that commit the pixel-exact gate did not LINK - and
 * a gate that does not build reports nothing at all rather than
 * reporting a failure.  It had been silent ever since.
 */
MMINTEGER mm_fb_readr(MMINTEGER offset, MMINTEGER len, MMINTEGER rows,
                      MMINTEGER stride, void *buf)
{
    unsigned char *d = buf;
    MMINTEGER i;

    for (i = 0; i < rows; i++) {
        MMINTEGER o = offset + i * stride;
        if (o < 0 || o + len > (MMINTEGER)cur_stride * cur_vres)
            return -1;
        memcpy(d + i * len, fb + o, (size_t)len);
    }
    return 0;
}

MMINTEGER mm_fb_putr(MMINTEGER offset, MMINTEGER len, MMINTEGER rows,
                     MMINTEGER stride, const void *buf)
{
    const unsigned char *s = buf;
    MMINTEGER i;

    for (i = 0; i < rows; i++) {
        MMINTEGER o = offset + i * stride;
        if (o < 0 || o + len > (MMINTEGER)cur_stride * cur_vres)
            return -1;
        memcpy(fb + o, s + i * len, (size_t)len);
    }
    return 0;
}

/* Any raise in this harness is a test failure: the deliberate error
 * paths are the .bas gate's job. */
void mm_error(const char *msg)
{
    fprintf(stderr, "blitharness: unexpected raise: %s\n", msg);
    exit(1);
}

void mm_fatal(const char *msg)
{
    fprintf(stderr, "blitharness: fatal: %s\n", msg);
    exit(1);
}

/* One buffer stands in for N, F and L: target switching is identity
 * here, so the FRAMEBUFFER form is exercised as an in-buffer copy and
 * what is being checked is its row logic, clipping and transparency. */
static MMINTEGER cur_target;
MMINTEGER mm_fb_cur(void) { return cur_target; }
void mm_fb_write(MMINTEGER which) { cur_target = which; }

#include "mmb_flash.h"
#include "mmb_blit.h"
#include "mmb_sprite.h"

/* ---- the independent pixel model ------------------------------------ */

static int fbget(int x, int y)
{
    unsigned char b;

    if (cur_bpp == 4) {
        b = fb[y * cur_stride + (x >> 1)];
        return (x & 1) ? (b & 15) : (b >> 4);
    }
    b = fb[y * cur_stride + (x >> 3)];
    return (b >> (7 - (x & 7))) & 1;
}

static void fbset(int x, int y, int c)
{
    unsigned char *p;

    if (cur_bpp == 4) {
        p = &fb[y * cur_stride + (x >> 1)];
        if (x & 1)
            *p = (*p & 0xF0) | (c & 15);
        else
            *p = (*p & 0x0F) | ((c & 15) << 4);
    } else {
        p = &fb[y * cur_stride + (x >> 3)];
        if (c)
            *p |= 0x80 >> (x & 7);
        else
            *p &= ~(0x80 >> (x & 7));
    }
}

/* a snapshot to model against */
static unsigned char model[160 * 480];
static int mget(int x, int y)
{
    unsigned char b;

    if (cur_bpp == 4) {
        b = model[y * cur_stride + (x >> 1)];
        return (x & 1) ? (b & 15) : (b >> 4);
    }
    b = model[y * cur_stride + (x >> 3)];
    return (b >> (7 - (x & 7))) & 1;
}

static int failures;

static void expect(const char *what, int x, int y, int want, int got)
{
    if (want != got) {
        fprintf(stderr, "FAIL %s at %d,%d: want %d got %d (mode %d)\n",
                what, x, y, want, got, cur_bpp == 4 ? 2 : 1);
        failures++;
        if (failures > 20)
            exit(1);
    }
}

/* a deterministic test card: every pixel a function of x,y */
static void card(void)
{
    int x, y;

    for (y = 0; y < cur_vres; y++)
        for (x = 0; x < cur_hres; x++)
            fbset(x, y, cur_bpp == 4 ? ((x * 7 + y * 3 + (x >> 4)) & 15)
                                     : ((x ^ y) >> 2) & 1);
}

/* ---- the checks ----------------------------------------------------- */

/* READ then WRITE somewhere else, all four alignment parities, and
 * check every destination pixel against the source region. */
static void t_roundtrip(void)
{
    static const int cases[][6] = {
        /* sx, sy, w, h, dx, dy */
        {10, 10, 32, 16, 100, 50},
        {11, 10, 33, 16, 100, 50},   /* odd source x, odd width  */
        {10, 10, 32, 16, 101, 51},   /* odd destination x        */
        {11, 13, 29, 7,  101, 51},   /* odd everything           */
        {0, 0, 1, 1, 5, 5},          /* single pixel             */
    };
    int c, i, j;

    for (c = 0; c < 5; c++) {
        const int *k = cases[c];

        card();
        mmb_blit_read(1, k[0], k[1], k[2], k[3]);
        mmb_blit_write(1, k[4], k[5], 0);
        for (j = 0; j < k[3]; j++)
            for (i = 0; i < k[2]; i++)
                expect("roundtrip", k[4] + i, k[5] + j,
                       cur_bpp == 4
                           ? ((k[0] + i) * 7 + (k[1] + j) * 3 +
                              ((k[0] + i) >> 4)) & 15
                           : (((k[0] + i) ^ (k[1] + j)) >> 2) & 1,
                       fbget(k[4] + i, k[5] + j));
        mmb_blit_close(1);
    }
}

/* WRITE modes 1-7: mirrors and don't-copy-black against the model. */
static void t_modes(void)
{
    int mode, i, j;
    int w = 13, h = 9, sx = 20, sy = 20, dx = 60, dy = 40;

    for (mode = 1; mode <= 7; mode++) {
        card();
        mmb_blit_read(2, sx, sy, w, h);
        memcpy(model, fb, sizeof(model));
        mmb_blit_write(2, dx, dy, mode);
        for (j = 0; j < h; j++)
            for (i = 0; i < w; i++) {
                int bi = (mode & 1) ? w - 1 - i : i;
                int bj = (mode & 2) ? h - 1 - j : j;
                int src = mget(sx + bi, sy + bj);
                int want = ((mode & 4) && src == 0)
                               ? mget(dx + i, dy + j) : src;

                expect("mode", dx + i, dy + j, want, fbget(dx + i, dy + j));
            }
        mmb_blit_close(2);
    }
}

/* WRITE clipped off every edge: nothing outside may change, everything
 * inside must match. */
static void t_clip(void)
{
    static const int at[][2] = {
        {-5, -3}, {-5, 100}, {100, -3},
        {0, 0},
    };
    int c, x, y, i, j;
    int w = 16, h = 12, sx = 40, sy = 30;

    for (c = 0; c < 4; c++) {
        int dx = at[c][0] == 0 ? cur_hres - 7 : at[c][0];
        int dy = at[c][1] == 0 ? cur_vres - 5 : at[c][1];

        card();
        mmb_blit_read(3, sx, sy, w, h);
        memcpy(model, fb, sizeof(model));
        mmb_blit_write(3, dx, dy, 0);
        for (y = 0; y < cur_vres; y++)
            for (x = 0; x < cur_hres; x++) {
                int want = mget(x, y);

                i = x - dx;
                j = y - dy;
                if (i >= 0 && i < w && j >= 0 && j < h)
                    want = mget(sx + i, sy + j);
                if (want != fbget(x, y)) {
                    expect("clip", x, y, want, fbget(x, y));
                    y = cur_vres;
                    break;
                }
            }
        mmb_blit_close(3);
    }
}

/* plain BLIT with overlap, all four directions, against a snapshot. */
static void t_copy(void)
{
    static const int mv[][2] = { {6, 0}, {-6, 0}, {0, 4}, {0, -4} };
    int c, x, y;
    int sx = 50, sy = 50, w = 40, h = 30;

    for (c = 0; c < 4; c++) {
        int dx = sx + mv[c][0], dy = sy + mv[c][1];

        card();
        memcpy(model, fb, sizeof(model));
        mmb_blit_copy(sx, sy, dx, dy, w, h);
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
                expect("copy", dx + x, dy + y,
                       mget(sx + x, sy + y), fbget(dx + x, dy + y));
    }
}

/* the decoders: a known image RLE- and raw-encoded by this harness,
 * decoded by the engine, transparent colour honoured. */
static void t_decode(void)
{
    /* 8x4 pixels, drawn with colour 5 transparent */
    static const unsigned char img[32] = {
        1, 2, 3, 4, 4, 4, 4, 5,
        5, 5, 1, 1, 1, 1, 2, 2,
        7, 7, 7, 7, 7, 7, 7, 7,
        0, 1, 5, 1, 0, 1, 5, 1,
    };
    unsigned char stream[80];
    int n, i, x, y;

    if (cur_bpp != 4)
        return;

    /* RLE encode: runs capped under 15 */
    n = 0;
    stream[n++] = 8 & 0xFF; stream[n++] = 0;          /* w = 8  */
    stream[n++] = 4 & 0xFF; stream[n++] = 0x80;       /* h = 4, compressed */
    i = 0;
    while (i < 32) {
        int run = 1;

        while (i + run < 32 && img[i + run] == img[i] && run < 15)
            run++;
        stream[n++] = (unsigned char)((img[i] << 4) | run);
        i += run;
    }
    card();
    memcpy(model, fb, sizeof(model));
    mmb_blit_mem((MMINTEGER)(long)stream, 30, 30, 5);
    for (y = 0; y < 4; y++)
        for (x = 0; x < 8; x++)
            expect("rle", 30 + x, 30 + y,
                   img[y * 8 + x] == 5 ? mget(30 + x, 30 + y)
                                       : img[y * 8 + x],
                   fbget(30 + x, 30 + y));

    /* raw encode: low nibble first */
    n = 0;
    stream[n++] = 8; stream[n++] = 0;
    stream[n++] = 4; stream[n++] = 0;
    for (i = 0; i < 32; i += 2)
        stream[n++] = (unsigned char)(img[i] | (img[i + 1] << 4));
    card();
    memcpy(model, fb, sizeof(model));
    mmb_blit_mem((MMINTEGER)(long)stream, 100, 100, -1);
    for (y = 0; y < 4; y++)
        for (x = 0; x < 8; x++)
            expect("raw", 100 + x, 100 + y, img[y * 8 + x],
                   fbget(100 + x, 100 + y));

    /* the same raw image clipped off the left edge */
    card();
    memcpy(model, fb, sizeof(model));
    mmb_blit_mem((MMINTEGER)(long)stream, -3, 10, -1);
    for (y = 0; y < 4; y++)
        for (x = 0; x < 8; x++) {
            int want = (x - 3 >= 0) ? img[y * 8 + x] : mget(0, 0);

            if (x - 3 >= 0)
                expect("rawclip", x - 3, 10 + y, want,
                       fbget(x - 3, 10 + y));
        }
    (void)i;
}

/* clipped READ keeps the clipped size and the right pixels */
static void t_readclip(void)
{
    int i, j;

    card();
    mmb_blit_read(4, -4, -6, 32, 16);
    if (mmb_bb[3].w != 28 || mmb_bb[3].h != 10) {
        fprintf(stderr, "FAIL readclip: dims %dx%d want 28x10\n",
                mmb_bb[3].w, mmb_bb[3].h);
        failures++;
    }
    for (j = 0; j < 10; j++)
        for (i = 0; i < 28; i++)
            expect("readclip", i, j, fbget(i, j),
                   mmb_bb[3].px[j * 28 + i]);
    mmb_blit_close(4);
}

/* BLIT FLASH out of a pseudo slot: the slot image is packed the
 * PicoMite way - LOW nibble is the left pixel, the mirror of this
 * machine - and this check is what pins that interpretation. */
static void t_flash(void)
{
    unsigned char *s;
    int x, y;

    if (cur_bpp != 4)
        return;
    s = mmf_addr(1);
    /* a 6x3 image: pixel (x,y) = x + y + 1, low nibble first */
    s[0] = 6; s[1] = 0; s[2] = 0; s[3] = 0;
    s[4] = 3; s[5] = 0; s[6] = 0; s[7] = 0;
    for (y = 0; y < 3; y++)
        for (x = 0; x < 6; x += 2)
            s[8 + y * 3 + (x >> 1)] =
                (unsigned char)(((x + y + 1) & 15) |
                                (((x + 1 + y + 1) & 15) << 4));
    card();
    memcpy(model, fb, sizeof(model));
    mmb_blit_flash(1, 0, 0, 0, 40, 40, 6, 3, -1);
    for (y = 0; y < 3; y++)
        for (x = 0; x < 6; x++)
            expect("flash", 40 + x, 40 + y, (x + y + 1) & 15,
                   fbget(40 + x, 40 + y));

    /* transparent: colour 2 keeps the card underneath */
    card();
    memcpy(model, fb, sizeof(model));
    mmb_blit_flash(1, 0, 0, 0, 60, 40, 6, 3, 2);
    for (y = 0; y < 3; y++)
        for (x = 0; x < 6; x++) {
            int c = (x + y + 1) & 15;

            expect("flash-t", 60 + x, 40 + y,
                   c == 2 ? mget(60 + x, 40 + y) : c,
                   fbget(60 + x, 40 + y));
        }
    mmf_erase(1);
}

/* the FRAMEBUFFER form as an in-buffer rectangle copy with clipping
 * and transparency (the mock has one buffer, see mm_fb_write above) */
static void t_fbform(void)
{
    int x, y;

    if (cur_bpp != 4)
        return;
    card();
    memcpy(model, fb, sizeof(model));
    mmb_blit_fb(0, 1, 30, 30, 200, 100, 20, 10, -1);
    for (y = 0; y < 10; y++)
        for (x = 0; x < 20; x++)
            expect("fbform", 200 + x, 100 + y,
                   mget(30 + x, 30 + y), fbget(200 + x, 100 + y));

    card();
    memcpy(model, fb, sizeof(model));
    mmb_blit_fb(0, 1, 30, 30, 200, 100, 20, 10, 3);
    for (y = 0; y < 10; y++)
        for (x = 0; x < 20; x++) {
            int c = mget(30 + x, 30 + y);

            expect("fbform-t", 200 + x, 100 + y,
                   c == 3 ? mget(200 + x, 100 + y) : c,
                   fbget(200 + x, 100 + y));
        }
}

/* The sprite pixel path: show composites with transparency and saves
 * the background, a second show restores the first spot, hide restores
 * everything, opaque flags copy the zeros too. */
static void t_sprite(void)
{
    MMINTEGER img[64];
    int x, y, i;

    if (cur_bpp != 4)
        return;
    /* colour 9 checkerboard, transparent (0) elsewhere */
    for (i = 0; i < 64; i++)
        img[i] = ((i ^ (i >> 3)) & 1) ? 0xFF00FF : 0;  /* magenta = 9 */
    card();
    memcpy(model, fb, sizeof(model));
    mms_loadarray(1, 8, 8, img, 64);
    mms_show(1, 20, 20, 1, 0, 0, 0);
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++) {
            int on = ((y * 8 + x) ^ ((y * 8 + x) >> 3)) & 1;

            expect("spr-show", 20 + x, 20 + y,
                   on ? 9 : mget(20 + x, 20 + y), fbget(20 + x, 20 + y));
        }
    /* moving restores the old spot exactly */
    mms_show(1, 40, 30, 1, 0, 0, 0);
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            expect("spr-move", 20 + x, 20 + y, mget(20 + x, 20 + y),
                   fbget(20 + x, 20 + y));
    /* hide restores everything */
    mms_hide(1, 0);
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            expect("spr-hide", 40 + x, 30 + y, mget(40 + x, 30 + y),
                   fbget(40 + x, 30 + y));
    /* opaque show copies the zeros too */
    mms_show(1, 60, 60, 1, 4, 0, 0);
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++) {
            int on = ((y * 8 + x) ^ ((y * 8 + x) >> 3)) & 1;

            expect("spr-opaque", 60 + x, 60 + y, on ? 9 : 0,
                   fbget(60 + x, 60 + y));
        }
    mms_hide(1, 0);
    mms_close(1);
}

int main(void)
{
    int m;

    for (m = 1; m <= 2; m++) {
        setmode(m);
        t_roundtrip();
        t_modes();
        t_clip();
        t_copy();
        t_decode();
        t_readclip();
        t_flash();
        t_fbform();
        t_sprite();
    }
    if (failures) {
        fprintf(stderr, "blitharness: %d failures\n", failures);
        return 1;
    }
    printf("blitharness: all checks passed\n");
    return 0;
}

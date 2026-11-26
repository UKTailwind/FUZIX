/*
 * FRAMEBUFFER WARPH, WARPV, AFFINE and SPAN - MMBasic V8's raster
 * commands, for the PC3 display.  The portable half, beside display.c:
 * plain byte buffers, nothing of the scanout.
 *
 *   WARPH   each row of one buffer copied to another, moved sideways by
 *           its band's offset (and optionally taken from another row)
 *   WARPV   each column moved up or down by its band's offset
 *   AFFINE  each row read from the source along a straight line, so one
 *           band turns and zooms a whole picture and a band a row draws
 *           a floor in perspective
 *   SPAN    each row copied only between its band's left and right
 *           columns (or outside them), the rest left or filled
 *
 * and BLIT RESIZE, a rectangle stretched or shrunk onto another, which
 * the same programs use (GFXIOC_RESIZE).
 *
 * The PicoMite's graphics/Warp.c is the reference, and the design is its
 * docs/FRAMEBUFFER_WARP_Design.md and docs/FRAMEBUFFER_AFFINE_SPAN_Design.md.
 * This is that core reduced to the one depth this display draws MMBasic
 * programs in - four bits a pixel, MODE 2 and (on a PC) MODE 3 - with
 * every pixel and byte placed through display_priv.h's PX4_ macros, so the
 * same file is right for the board's nibble order and for a PC's, which is
 * the PicoMite's own.  The one-bit modes are refused, as MMBasic refuses
 * MODE 1.
 *
 * Who checks what: the runtime raises MMBasic's errors, in MMBasic's
 * order and words, and hands over only what is legal - WARP's offsets
 * already reduced and in band order, AFFINE's and SPAN's bands cut to
 * those that can reach the screen.  This file checks it all again,
 * because a request is untrusted, and refuses with -1 rather than draw
 * outside a buffer.
 *
 * WARP_CORE_TEST compiles the core alone, with the test supplying the
 * PX4_ macros, so it can be checked against a pixel model in either
 * order (pc3host tests/seam/warp_seam.c).
 */
#ifndef WARP_CORE_TEST
#include <stdint.h>
#include <string.h>
#include "display.h"
#include "display_priv.h"
#include "pico_ioctl.h"
#endif

#define WARP_WRAP	(-2)		/* edge: round the source (the default) */
#define WARP_LEAVE	(-1)		/* edge: leave the destination's pixel  */
#define WARP_FAR	(1 << 24)	/* first, left and width go no further  */

/* The nibbles of a byte: the bits that hold its even (left) pixel and
 * its odd one, and a byte made of two pixels. */
#define WP_EVEN_MASK	((uint8_t)~PX4_EVEN_KEEP)
#define WP_ODD_MASK	((uint8_t)~PX4_ODD_KEEP)
#define WP_PAIR(e, o)	((uint8_t)(PX4_EVEN(e) | PX4_ODD(o)))

/* Always inlined: on the PicoMite a call for each pixel cost more than
 * the pixel. */
#define WP_INLINE static inline __attribute__((always_inline))

WP_INLINE int wp_get(const uint8_t *row, int x)
{
    unsigned v = row[x >> 1];

    return (x & 1) ? (int)PX4_ODD_GET(v) : (int)PX4_EVEN_GET(v);
}

WP_INLINE void wp_put(uint8_t *row, int x, int c)
{
    uint8_t *p = row + (x >> 1);

    *p = (x & 1) ? (uint8_t)((*p & PX4_ODD_KEEP) | PX4_ODD(c))
                 : (uint8_t)((*p & PX4_EVEN_KEEP) | PX4_EVEN(c));
}

/* w pixels from pixel sx of a source row to pixel dx of a destination
 * row; those of value t (-1: none) are not written.  The rows are in
 * different buffers. */
static void wp_seg(uint8_t *d, int dx, const uint8_t *s, int sx, int w, int t)
{
    const uint8_t *r;
    uint8_t *q;
    int c, n, odd, i;

    if (w <= 0)
        return;
    if (dx & 1) {               /* an odd start: the destination's odd pixel alone */
        if ((c = wp_get(s, sx)) != t)
            wp_put(d, dx, c);
        dx++, sx++, w--;
    }
    q = d + (dx >> 1);          /* dx is even now: whole destination bytes */
    r = s + (sx >> 1);
    n = w >> 1;
    odd = sx & 1;
    if (t < 0 && !odd)
        memcpy(q, r, (unsigned)n);
    else if (t < 0)             /* an odd source start: each byte from two */
        for (i = 0; i < n; i++)
            q[i] = WP_PAIR(PX4_ODD_GET((unsigned)r[i]),
                           PX4_EVEN_GET((unsigned)r[i + 1]));
    else
        for (i = 0; i < n; i++) {
            /* as above, with a transparent pixel masked out, so the
             * byte is merged whole */
            uint8_t v = odd ? WP_PAIR(PX4_ODD_GET((unsigned)r[i]),
                                      PX4_EVEN_GET((unsigned)r[i + 1]))
                            : r[i];
            uint8_t m = 0xFF;

            if ((int)PX4_EVEN_GET((unsigned)v) == t)
                m &= PX4_EVEN_KEEP;
            if ((int)PX4_ODD_GET((unsigned)v) == t)
                m &= PX4_ODD_KEEP;
            q[i] = (uint8_t)((q[i] & ~m) | (v & m));
        }
    if ((w & 1) && (c = wp_get(s, sx + w - 1)) != t)   /* an odd end */
        wp_put(d, dx + w - 1, c);
}

/* w pixels of colour c from pixel x: whole bytes between an odd start
 * and an odd end (SPAN fills most of a screen). */
static void wp_fill(uint8_t *row, int x, int w, int c)
{
    if (w <= 0)
        return;
    if (x & 1) {
        wp_put(row, x, c);
        x++, w--;
    }
    memset(row + (x >> 1), c * 0x11, (unsigned)(w >> 1));
    if (w & 1)
        wp_put(row, x + w - 1, c);
}

/* ---- WARPH and WARPV ------------------------------------------------------
 * Band b is the step rows (WARPH) or columns (WARPV) from first + b * step,
 * the last cut at the buffer's edge.  A destination pixel (x, y) is the
 * source's (x - off, sy) in WARPH, sy being y or the band's row from rows,
 * and (x, y + off) in WARPV: positive moves right, and up.  With no edge
 * the source wraps round; WARP_LEAVE leaves the pixels nothing moves onto,
 * and a colour fills them.  A source pixel of the transparent value is not
 * written. */
struct wp_warp {
    uint8_t *dst;
    const uint8_t *src;
    int w, h, stride;
    int vertical;
    int n, step, first;
    const int32_t *boff;        /* 0..size-1 when wrapping, else -size..size */
    const int32_t *brow;        /* WARPH: each band's first source row, -1 to
                                   leave the band; NULL for none */
    int edge, trans;            /* WARP_WRAP, WARP_LEAVE or a pixel; -1 or a pixel */
};

/* WARPV, row by row from the top, so the destination is written down
 * the screen.  A band's pixels keep their x, so a band moves whole bytes
 * between its ends. */
static void wp_v(const struct wp_warp *a)
{
    const uint8_t *src = a->src;        /* in locals: a store through a */
    const int32_t *boff = a->boff;      /* byte pointer could change *a */
    int h = a->h, wd = a->w, stride = a->stride, n = a->n, step = a->step;
    int edge = a->edge, trans = a->trans, y, b, x;

    if (edge == WARP_WRAP && trans < 0 && !((a->first | step) & 1)) {
        /* the usual wave: an even first column and step, so each band is
         * whole bytes of one source row */
        for (y = 0; y < h; y++) {
            uint8_t *d = a->dst + y * stride;

            for (b = 0, x = a->first; b < n; b++, x += step) {
                int sy = y + boff[b], k = x >> 1;
                int e = (x + step > wd ? wd : x + step) >> 1;
                const uint8_t *s = src + (sy >= h ? sy - h : sy) * stride;

                for (; k < e; k++)
                    d[k] = s[k];
            }
        }
        return;
    }
    for (y = 0; y < h; y++) {
        uint8_t *d = a->dst + y * stride;

        for (b = 0, x = a->first; b < n; b++, x += step) {
            int e = x + step > wd ? wd : x + step, sy = y + boff[b], i = x, c;
            const uint8_t *s;

            if (edge == WARP_WRAP) {
                if (sy >= h)
                    sy -= h;
            } else if (sy < 0 || sy >= h) {
                if (edge >= 0)
                    for (; i < e; i++)
                        wp_put(d, i, edge);
                continue;
            }
            s = src + sy * stride;
            if (trans < 0) {
                if (i & 1) {
                    wp_put(d, i, wp_get(s, i));
                    i++;
                }
                for (; i + 1 < e; i += 2)
                    d[i >> 1] = s[i >> 1];
                if (i < e)
                    wp_put(d, i, wp_get(s, i));
            } else
                for (; i < e; i++)
                    if ((c = wp_get(s, i)) != trans)
                        wp_put(d, i, c);
        }
    }
}

/* WARPV with step 1, a column a band, where wp_v's band overhead would be
 * paid for each pixel.  In the wrapping copy each destination byte is made
 * from two columns at once. */
static void wp_v1(const struct wp_warp *a)
{
    const uint8_t *src = a->src;
    const int32_t *boff = a->boff;
    int h = a->h, stride = a->stride, n = a->n, edge = a->edge, trans = a->trans;
    int y;

    for (y = 0; y < h; y++) {
        uint8_t *d = a->dst + y * stride;
        int b = 0, x = a->first;

        if (edge == WARP_WRAP && trans < 0) {
            if (x & 1) {                /* an odd first column alone */
                int sy = y + boff[b++];

                wp_put(d, x, wp_get(src + (sy >= h ? sy - h : sy) * stride, x));
                x++;
            }
            for (; b + 1 < n; b += 2, x += 2) {
                int s0 = y + boff[b], s1 = y + boff[b + 1];

                if (s0 >= h)
                    s0 -= h;
                if (s1 >= h)
                    s1 -= h;
                d[x >> 1] = (uint8_t)((src[s0 * stride + (x >> 1)] & WP_EVEN_MASK) |
                                      (src[s1 * stride + (x >> 1)] & WP_ODD_MASK));
            }
        }
        for (; b < n; b++, x++) {       /* the general case, and an odd last column */
            int sy = y + boff[b], c;

            if (edge == WARP_WRAP) {
                if (sy >= h)
                    sy -= h;
            } else if (sy < 0 || sy >= h) {
                if (edge >= 0)
                    wp_put(d, x, edge);
                continue;
            }
            if ((c = wp_get(src + sy * stride, x)) != trans)
                wp_put(d, x, c);
        }
    }
}

/* one row of WARPH: the destination's pixel x is the source's x - o */
static void wp_row(const struct wp_warp *a, uint8_t *d, const uint8_t *s, int o)
{
    int w = a->w;

    if (a->edge == WARP_WRAP) {         /* o is 0 to w-1 */
        wp_seg(d, o, s, 0, w - o, a->trans);
        wp_seg(d, 0, s, w - o, o, a->trans);
    } else if (o >= 0) {                /* o is -w to w */
        wp_seg(d, o, s, 0, w - o, a->trans);
        if (a->edge >= 0)
            wp_fill(d, 0, o, a->edge);
    } else {
        wp_seg(d, 0, s, -o, w + o, a->trans);
        if (a->edge >= 0)
            wp_fill(d, w + o, -o, a->edge);
    }
}

static void wp_warp_run(const struct wp_warp *a)
{
    int b, y;

    if (a->vertical) {
        if (a->step == 1)
            wp_v1(a);
        else
            wp_v(a);
        return;
    }
    for (b = 0; b < a->n; b++) {
        int y0 = a->first + b * a->step, y1 = y0 + a->step;
        int r = a->brow ? a->brow[b] : 0;

        if (a->brow && r < 0)
            continue;                   /* row -1: the band is left as it is */
        if (y1 > a->h)
            y1 = a->h;
        for (y = y0; y < y1; y++) {
            int sy = y;

            /* the band's next source rows, round to the top */
            if (a->brow && (sy = r + y - y0) >= a->h)
                sy -= a->h;
            wp_row(a, a->dst + y * a->stride, a->src + sy * a->stride, a->boff[b]);
        }
    }
}

/* ---- SPAN -----------------------------------------------------------------
 * Band b is the step rows from first + b * step (those off the buffer are
 * not drawn), and its span is the columns lf[b] to rt[b], cut to the row
 * and empty when lf > rt.  The span's pixels (outside: the row's others)
 * are copied from the same place in the source, those of the transparent
 * value excepted; the row's other pixels get the fill colour, if there is
 * one.  The source may be the destination: nothing is copied then, and
 * only the fill is drawn. */
struct wp_span {
    uint8_t *dst;
    const uint8_t *src;
    int w, h, stride;
    int n, step, first;
    const int64_t *lf, *rt;
    int fill, trans, outside;   /* -1 or a pixel; -1 or a pixel; 0 or 1 */
};

static void wp_span_run(const struct wp_span *a)
{
    int w = a->w, b, y;

    for (b = 0; b < a->n; b++) {
        int64_t y0 = a->first + (int64_t)b * a->step, y1 = y0 + a->step;
        int64_t l = a->lf[b], r = a->rt[b];
        int lo, hi;

        if (y0 >= a->h)
            break;
        if (y1 <= 0)
            continue;
        if (y0 < 0)
            y0 = 0;
        if (y1 > a->h)
            y1 = a->h;
        /* the span is lo to hi-1 */
        lo = l < 0 ? 0 : l > w ? w : (int)l;
        hi = r < 0 ? 0 : r >= w ? w : (int)r + 1;
        if (hi < lo)
            hi = lo;
        for (y = (int)y0; y < (int)y1; y++) {
            uint8_t *d = a->dst + y * a->stride;
            const uint8_t *s = a->src + y * a->stride;

            if (!a->outside) {
                if (s != d)
                    wp_seg(d, lo, s, lo, hi - lo, a->trans);
                if (a->fill >= 0) {
                    wp_fill(d, 0, lo, a->fill);
                    wp_fill(d, hi, w - hi, a->fill);
                }
            } else {
                if (s != d) {
                    wp_seg(d, 0, s, 0, lo, a->trans);
                    wp_seg(d, hi, s, hi, w - hi, a->trans);
                }
                if (a->fill >= 0)
                    wp_fill(d, lo, hi - lo, a->fill);
            }
        }
    }
}

/* ---- AFFINE ---------------------------------------------------------------
 * Band b is the step rows from first + b * step, and the window is the
 * width columns from left; what is off the buffer is not drawn.  Column i
 * of row j of the band shows the source's pixel (sx, sy):
 *
 *     U = u[b] - j * dv[b] + i * du[b]      sx = U / 65536, rounded down
 *     V = v[b] + j * du[b] + i * dv[b]      sy = V / 65536, rounded down
 *
 * With no edge (WARP_WRAP) sx and sy are taken round the source, and any
 * value is allowed.  With an edge a pixel whose place is outside the source
 * is left (WARP_LEAVE) or gets the edge colour, and a place beyond
 * +-AFF_POS or a step beyond +-AFF_STEP is taken as that limit, so that no
 * sum here can overflow.  A source pixel of the transparent value is not
 * written. */
#define AFF_POS		((int64_t)1 << 60)
#define AFF_STEP	((int64_t)1 << 35)

struct wp_affine {
    uint8_t *dst;
    const uint8_t *src;
    int w, h, stride;
    int n, step, first, left, width;    /* the bands, and the window */
    const int64_t *u, *v, *du, *dv;     /* each band: the place its first
                                           pixel shows, and the step along
                                           a row */
    int edge, trans;
};

static int64_t aff_mod(int64_t a, int64_t m)    /* 0 to m-1 */
{
    a %= m;
    return a < 0 ? a + m : a;
}

static int64_t aff_floor(int64_t a, int64_t b)  /* a / b rounded down, b > 0 */
{
    int64_t q = a / b;

    return a % b < 0 ? q - 1 : q;
}

static int64_t aff_clamp(int64_t a, int64_t m)
{
    return a > m ? m : a < -m ? -m : a;
}

/* the columns *i0 to *i1-1 cut to those i for which f + i * d is 0 to
 * m-1 (a straight line: one run of them) */
static void aff_inside(int64_t f, int64_t d, int64_t m, int64_t *i0, int64_t *i1)
{
    int64_t lo, hi;

    if (d == 0) {
        if (f < 0 || f >= m)
            *i1 = *i0;
        return;
    }
    if (d > 0)
        lo = -aff_floor(f, d), hi = aff_floor(m - 1 - f, d);
    else
        lo = -aff_floor(m - 1 - f, -d), hi = aff_floor(f, -d);
    if (lo > *i0)
        *i0 = lo;
    if (hi + 1 < *i1)
        *i1 = hi + 1;
}

struct aff_step {
    uint32_t du, dv, mw, mh;    /* the steps along a row, and the source's
                                   size: all in 1/65536 pixel */
    const uint8_t *src;
    int stride, trans;
};

/* cnt pixels of a destination row from pixel x: each is the source's
 * pixel at (U >> 16, V >> 16), and U and V then step by du and dv round
 * the source.  All four are 0 to mw-1 or mh-1, so a step is an addition
 * and one compare, and no pixel needs a division.  A function of its own,
 * as on the PicoMite: inlined into affine_run its loop ran out of
 * registers. */
static __attribute__((noinline)) void aff_row(uint8_t *d, int x, int cnt,
                                              uint32_t U, uint32_t V,
                                              const struct aff_step *s)
{
    uint32_t du = s->du, dv = s->dv, mw = s->mw, mh = s->mh;
    const uint8_t *src = s->src;
    int stride = s->stride, trans = s->trans, c, c2;
    uint8_t *q;

#define AFF_NEXT(c)                                     \
    {                                                   \
        c = wp_get(src + (V >> 16) * stride, U >> 16);  \
        if ((U += du) >= mw)                            \
            U -= mw;                                    \
        if ((V += dv) >= mh)                            \
            V -= mh;                                    \
    }
    if (trans < 0) {            /* two pixels for each whole destination byte */
        if (cnt && (x & 1)) {
            AFF_NEXT(c);
            wp_put(d, x, c);
            x++, cnt--;
        }
        q = d + (x >> 1);
        for (; cnt >= 2; cnt -= 2) {
            AFF_NEXT(c);
            AFF_NEXT(c2);
            *q++ = WP_PAIR(c, c2);
        }
        if (cnt) {
            AFF_NEXT(c);
            *q = (uint8_t)((*q & PX4_EVEN_KEEP) | PX4_EVEN(c));
        }
        return;
    }
    for (; cnt > 0; cnt--, x++) {
        AFF_NEXT(c);
        if (c != trans)
            wp_put(d, x, c);
    }
#undef AFF_NEXT
}

static void wp_affine_run(const struct wp_affine *a)
{
    int w = a->w, h = a->h, wrap = a->edge == WARP_WRAP, b, j;
    int64_t mw = (int64_t)w << 16, mh = (int64_t)h << 16;
    struct aff_step st;
    /* the window's columns that are on the buffer */
    int64_t ia = a->left < 0 ? -(int64_t)a->left : 0, ib = a->width;

    st.du = st.dv = 0;
    st.mw = (uint32_t)mw;
    st.mh = (uint32_t)mh;
    st.src = a->src;
    st.stride = a->stride;
    st.trans = a->trans;
    if (ib > (int64_t)w - a->left)
        ib = (int64_t)w - a->left;
    if (ia >= ib)
        return;
    for (b = 0; b < a->n; b++) {
        int64_t y0 = a->first + (int64_t)b * a->step;
        int64_t u = a->u[b], v = a->v[b], du = a->du[b], dv = a->dv[b];
        int64_t dvu = dv, duv = du;     /* the steps down the band: dv as U
                                           takes it, du as V does */
        int j1;

        if (y0 >= h)
            break;
        if (y0 + a->step <= 0)
            continue;
        if (wrap) {
            u = aff_mod(u, mw), v = aff_mod(v, mh);
            dvu = aff_mod(dv, mw), duv = aff_mod(du, mh);
        } else {
            u = aff_clamp(u, AFF_POS), v = aff_clamp(v, AFF_POS);
            dvu = dv = aff_clamp(dv, AFF_STEP);
            duv = du = aff_clamp(du, AFF_STEP);
        }
        /* the steps along a row, round the source.  With an edge these
         * serve only for a run of two pixels or more inside the source,
         * whose step is then smaller than the source: the same step, a
         * negative one made positive */
        st.du = (uint32_t)aff_mod(du, mw);
        st.dv = (uint32_t)aff_mod(dv, mh);
        j1 = y0 + a->step > h ? (int)(h - y0) : a->step;
        for (j = y0 < 0 ? (int)-y0 : 0; j < j1; j++) {
            uint8_t *d = a->dst + (int)(y0 + j) * a->stride;
            int64_t U = u - j * dvu, V = v + j * duv, i0 = ia, i1 = ib;

            if (wrap) {
                U = aff_mod(U + i0 * st.du, mw);
                V = aff_mod(V + i0 * st.dv, mh);
            } else {
                aff_inside(U, du, mw, &i0, &i1);
                aff_inside(V, dv, mh, &i0, &i1);
                if (i0 >= i1)
                    i0 = i1 = ia;       /* none of the row is in the source */
                if (a->edge >= 0) {
                    wp_fill(d, a->left + (int)ia, (int)(i0 - ia), a->edge);
                    wp_fill(d, a->left + (int)i1, (int)(ib - i1), a->edge);
                }
                U += i0 * du, V += i0 * dv;
            }
            aff_row(d, a->left + (int)i0, (int)(i1 - i0), (uint32_t)U, (uint32_t)V, &st);
        }
    }
}

/* ---- BLIT RESIZE ----------------------------------------------------------
 * The rectangle sw x sh at (sx, sy) of the source stretched or shrunk onto
 * dw x dh at (dx, dy) of the destination, nearest pixel: pixel k of the
 * destination rectangle (across; likewise down) shows pixel k * sw / dw
 * of the source rectangle, rounded down.  The place is kept exactly, as a
 * whole part and a remainder out of dw that carries - the PicoMite's
 * graphics/Blit.c as fixed in V8.00.00b3.  The destination is cut to the
 * buffer; the source must be wholly on it.  A pixel of the transparent
 * value is not written.  When the two rectangles overlap in one buffer
 * the source is read from a copy, taken first, into scratch: one row of
 * sw pixels for each of sh rows, packed. */
struct wp_resize {
    uint8_t *dst;
    const uint8_t *src;
    int w, h, stride;
    int sx, sy, sw, sh, dx, dy, dw, dh, trans;
    uint8_t *scratch;           /* (sw + 1) / 2 * sh bytes, for an overlap */
};

static void wp_resize_run(const struct wp_resize *a)
{
    int x0 = a->dx < 0 ? 0 : a->dx, y0 = a->dy < 0 ? 0 : a->dy;
    int64_t xe = (int64_t)a->dx + a->dw, ye = (int64_t)a->dy + a->dh;
    int x1 = xe > a->w ? a->w : (int)xe, y1 = ye > a->h ? a->h : (int)ye;
    const uint8_t *s = a->src;
    int sstride = a->stride, sx = a->sx, sy = a->sy, x, y;
    unsigned xw = (unsigned)(a->sw / a->dw), xr = (unsigned)(a->sw % a->dw);
    unsigned yw = (unsigned)(a->sh / a->dh), yr = (unsigned)(a->sh % a->dh);
    unsigned xp0, yp;
    int64_t t;
    int srcy, srcx0;

    if (x0 >= x1 || y0 >= y1)
        return;
    if (a->src == a->dst && x0 < sx + a->sw && x1 > sx && y0 < sy + a->sh && y1 > sy) {
        /* an overlap: read from a copy of the source rectangle */
        int cs = (a->sw + 1) >> 1;

        for (y = 0; y < a->sh; y++)
            for (x = 0; x < a->sw; x++)
                wp_put(a->scratch + y * cs, x, wp_get(a->src + (sy + y) * sstride, sx + x));
        s = a->scratch, sstride = cs, sx = sy = 0;
    }
    t = (int64_t)(y0 - a->dy) * a->sh;
    srcy = (int)(t / a->dh);
    yp = (unsigned)(t % a->dh);
    t = (int64_t)(x0 - a->dx) * a->sw;
    srcx0 = (int)(t / a->dw);
    xp0 = (unsigned)(t % a->dw);
    for (y = y0; y < y1; y++, srcy += yw, yp += yr) {
        uint8_t *d = a->dst + y * a->stride;
        const uint8_t *r;
        int srcx = srcx0;
        unsigned xp = xp0;

        if (yp >= (unsigned)a->dh)
            yp -= a->dh, srcy++;
        r = s + (sy + srcy) * sstride;
        for (x = x0; x < x1; x++, srcx += xw, xp += xr) {
            int c;

            if (xp >= (unsigned)a->dw)
                xp -= a->dw, srcx++;
            if ((c = wp_get(r, sx + srcx)) != a->trans)
                wp_put(d, x, c);
        }
    }
}

#ifndef WARP_CORE_TEST
/* ---- GFXIOC_RESIZE --------------------------------------------------------
 * 0 when done (or nothing to do), -1 refused with nothing drawn.  An
 * overlap within one buffer needs scratch, which a PC has to spare and
 * the board's kernel would not. */
int display_gfx_resize(struct p_tab *who, const struct gfx_resize *g)
{
#ifdef PC_HOST
    static uint8_t scratch[DISP_FB_POOL];
#endif
    struct wp_resize a;
    uint16_t w, h, stride;
    uint8_t bpp, mode;

    display_gfx_geom(&w, &h, &stride, &bpp, &mode);
    if (bpp != 4 || w == 0 || h == 0)
        return -1;
    a.src = display_fb_buffer(who, g->src);
    a.dst = display_fb_buffer(who, g->dst);
    if (a.src == NULL || a.dst == NULL || g->trans < -1 || g->trans > 15)
        return -1;
    if (g->sw < 1 || g->sh < 1 || g->dw < 1 || g->dh < 1)
        return 0;
    if (g->sx < 0 || g->sy < 0 || (int64_t)g->sx + g->sw > w ||
        (int64_t)g->sy + g->sh > h)
        return -1;
    a.w = w;
    a.h = h;
    a.stride = stride;
    a.sx = g->sx, a.sy = g->sy, a.sw = g->sw, a.sh = g->sh;
    a.dx = g->dx, a.dy = g->dy, a.dw = g->dw, a.dh = g->dh;
    a.trans = g->trans;
#ifdef PC_HOST
    a.scratch = scratch;
#else
    a.scratch = NULL;
    if (a.src == a.dst)
        return -1;
#endif
    wp_resize_run(&a);
    return 0;
}

/* ---- GFXIOC_WARP ----------------------------------------------------------
 * One request, any of the four commands.  The arrays are in g->a[] and
 * readable here: the server's copy of the payload on a PC.  0 when done,
 * -1 when anything is out of range - nothing has been drawn then. */
int display_gfx_warp(struct p_tab *who, const struct gfx_warp *g)
{
    uint16_t w, h, stride;
    uint8_t bpp, mode;
    uint8_t *src, *dst;
    int n = g->n, step = g->step, first = g->first, trans = g->trans;
    int edge, i;

    display_gfx_geom(&w, &h, &stride, &bpp, &mode);
    if (bpp != 4 || w == 0 || h == 0 || g->op > GFX_SPAN)
        return -1;
    src = display_fb_buffer(who, g->src);
    dst = display_fb_buffer(who, g->dst);
    if (src == NULL || dst == NULL)
        return -1;
    if (n == 0)
        return 0;
    if (trans < -1 || trans > 15 || step < 1 || g->a[0] == NULL)
        return -1;
    /* the edge (SPAN's fill): wrap, leave, or a colour as BOX draws it */
    edge = g->edge;
    if (edge >= 0)
        edge = display_gfx_map((uint32_t)edge & 0xFFFFFF);
    else if (edge < WARP_WRAP || (edge == WARP_WRAP && g->op == GFX_SPAN))
        return -1;

    if (g->op == GFX_WARPH || g->op == GFX_WARPV) {
        struct wp_warp a;
        int vertical = g->op == GFX_WARPV;
        int bands = vertical ? w : h, size = vertical ? h : w;
        const int32_t *off = g->a[0], *rows = vertical ? NULL : g->a[1];

        if (src == dst || step > bands || first < 0 || first >= bands ||
            (int64_t)(n - 1) * step + first >= bands)
            return -1;
        for (i = 0; i < n; i++) {
            if (edge == WARP_WRAP ? off[i] < 0 || off[i] >= size
                                  : off[i] < -size || off[i] > size)
                return -1;
            if (rows && (rows[i] < -1 || rows[i] >= h))
                return -1;
        }
        a.dst = dst;
        a.src = src;
        a.w = w;
        a.h = h;
        a.stride = stride;
        a.vertical = vertical;
        a.n = n;
        a.step = step;
        a.first = first;
        a.boff = off;
        a.brow = rows;
        a.edge = edge;
        a.trans = trans;
        wp_warp_run(&a);
        return 0;
    }
    if (step > h || first < -WARP_FAR || first > WARP_FAR)
        return -1;
    if (g->op == GFX_SPAN) {
        struct wp_span a;

        if (g->a[1] == NULL || g->outside > 1)
            return -1;
        a.dst = dst;
        a.src = src;
        a.w = w;
        a.h = h;
        a.stride = stride;
        a.n = n;
        a.step = step;
        a.first = first;
        a.lf = g->a[0];
        a.rt = g->a[1];
        a.fill = edge;
        a.trans = trans;
        a.outside = g->outside;
        wp_span_run(&a);
        return 0;
    }
    {
        struct wp_affine a;

        if (src == dst || g->a[1] == NULL || g->a[2] == NULL || g->a[3] == NULL ||
            g->left < -WARP_FAR || g->left > WARP_FAR ||
            g->width < 1 || g->width > WARP_FAR)
            return -1;
        a.dst = dst;
        a.src = src;
        a.w = w;
        a.h = h;
        a.stride = stride;
        a.n = n;
        a.step = step;
        a.first = first;
        a.left = g->left;
        a.width = g->width;
        a.u = g->a[0];
        a.v = g->a[1];
        a.du = g->a[2];
        a.dv = g->a[3];
        a.edge = edge;
        a.trans = trans;
        wp_affine_run(&a);
        return 0;
    }
}
#endif

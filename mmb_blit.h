#ifndef MMB_BLIT_H
#define MMB_BLIT_H
/*
 *	BLIT READ [#]n, x, y, w, h
 *	BLIT WRITE [#]n, x, y [, mode]
 *	BLIT CLOSE [#]n
 *	BLIT x1, y1, x2, y2, w, h
 *	BLIT COMPRESSED addr, x, y [, transparent]
 *	BLIT MEMORY addr, x, y [, transparent]
 *
 *	MMBasic's cmd_blit / blitother / docompressed (graphics/Blit.c) and
 *	blit121 / blit121_self (graphics/RGB121.c), on top of the runtime's
 *	raw framebuffer window: mm_fb_read / mm_fb_write against the
 *	CURRENT draw target, so a FRAMEBUFFER WRITE F redirects a blit the
 *	same way it redirects a BOX - MMBasic's WriteBuf semantics.
 *
 *	Two deliberate departures from the reference, both recorded in
 *	PLAN-games.md's divergence ledger:
 *
 *	- A buffer stores one NATIVE COLOUR INDEX per byte, not the 3-byte
 *	  RGB888 of the PicoMite.  For these 16-colour modes the round trip
 *	  is lossless and the buffer is a third the size; nothing at the
 *	  BASIC surface can tell.  It does mean a buffer read in one MODE
 *	  cannot be written in the other (the reference's RGB888 buffers
 *	  are mode-blind): that raises "Invalid blit buffer for this mode"
 *	  rather than drawing garbage.
 *	- "Don't copy black" (WRITE modes 4-7) tests native index 0 - bit 0
 *	  in MODE 1 - where the reference tests an RGB triple of zero.  In
 *	  these modes the two agree.
 *
 *	Everything goes through two row workhorses, and the packing rules
 *	live NOWHERE ELSE: PC3 4bpp is HIGH nibble = left pixel, the exact
 *	mirror of the PicoMite's RGB121 packing, and transcribing the
 *	reference's nibble arithmetic was the trap this shape avoids.  The
 *	slow paths cost one byte-unpack per pixel against a 2us syscall per
 *	row (blitbench, Phase 0): noise.
 *
 *	On the host there is no display: mm_fb_geom() says so, pixel work
 *	is skipped, and the buffer bookkeeping (allocate, dimensions,
 *	"Buffer in use", CLOSE) still runs, so the gates exercise the whole
 *	command surface - the same silence every other primitive keeps.
 */

#include <stdlib.h>
#include <string.h>

/* Six independent entry points share this header, so the ones a program
 * does not name must not warn under gcc: the same bargain mmb_comms.h
 * struck, with the same conditional - cc1 takes neither __inline__ nor
 * __attribute__, and does not need them: its dead-static rule drops the
 * unnamed entry points silently. */
#ifndef MMG_FN
#if defined(MM_FCC) || defined(MM_PC3)
#define MMG_FN static
#else
#define MMG_FN static __inline__ __attribute__((unused))
#endif
#endif

#define MMB_NBLIT 64		/* MAXBLITBUF, configuration.h:383 */

/* Named rather than anonymous: the Fuzix cc1 cannot declare an array
 * of an anonymous struct type (found the expensive way, on the board). */
struct mmb_blitbuf {
	unsigned char *px;	/* one native index per byte, w*h, row-major */
	short w, h;
	signed char bpp;	/* mode captured at READ; 0 = headless */
};
static struct mmb_blitbuf mmb_bb[MMB_NBLIT];

/* Geometry of the current draw target, refreshed per statement because
 * FRAMEBUFFER WRITE can move the target between statements.  Returns 0,
 * or -1 with the sizes still filled in (the host: real geometry, no
 * bytes behind it). */
MMG_FN int mmb_geom(int *stride, int *bpp, int *hres, int *vres)
{
	int g = (int)mm_fb_geom();

	*hres = (int)mm_hres();
	*vres = (int)mm_vres();
	if (g < 0)
		return -1;
	*stride = g >> 8;
	*bpp = g & 0xFF;
	return (*bpp == 1 || *bpp == 4) ? 0 : -1;
}

/* ---- the row workhorses ---------------------------------------------
 * A row segment as one byte per pixel.  All algorithms above this line
 * of the file think in pixels; only these two think in bytes. */

static unsigned char mmb_rowb[164];	/* widest byte span + slack */

MMG_FN int mmb_row_get(int y, int x0, int w, int stride, int bpp,
		       unsigned char *px)
{
	int b0, b1, i;

	if (bpp == 4) {
		b0 = x0 >> 1;
		b1 = (x0 + w - 1) >> 1;
	} else {
		b0 = x0 >> 3;
		b1 = (x0 + w - 1) >> 3;
	}
	if (mm_fb_read((MMINTEGER)y * stride + b0, b1 - b0 + 1, mmb_rowb) < 0)
		return -1;
	for (i = 0; i < w; i++) {
		int x = x0 + i;

		if (bpp == 4)
			px[i] = (x & 1) ? (mmb_rowb[(x >> 1) - b0] & 15)
					: (mmb_rowb[(x >> 1) - b0] >> 4);
		else
			px[i] = (mmb_rowb[(x >> 3) - b0] >> (7 - (x & 7))) & 1;
	}
	return 0;
}

MMG_FN int mmb_row_put(int y, int x0, int w, int stride, int bpp,
		       const unsigned char *px)
{
	int b0, b1, i;

	if (bpp == 4) {
		b0 = x0 >> 1;
		b1 = (x0 + w - 1) >> 1;
	} else {
		b0 = x0 >> 3;
		b1 = (x0 + w - 1) >> 3;
	}
	/* Read-modify-write, always: the boundary bytes carry pixels that
	 * are not ours, and one uniform path beats reasoning per case
	 * about which edges align (the pixel-batch lesson applied). */
	if (mm_fb_read((MMINTEGER)y * stride + b0, b1 - b0 + 1, mmb_rowb) < 0)
		return -1;
	for (i = 0; i < w; i++) {
		int x = x0 + i;

		if (bpp == 4) {
			unsigned char *p = &mmb_rowb[(x >> 1) - b0];

			if (x & 1)
				*p = (*p & 0xF0) | (px[i] & 15);
			else
				*p = (*p & 0x0F) | ((px[i] & 15) << 4);
		} else {
			unsigned char *p = &mmb_rowb[(x >> 3) - b0];
			unsigned char m = 0x80 >> (x & 7);

			if (px[i])
				*p |= m;
			else
				*p &= (unsigned char)~m;
		}
	}
	return mm_fb_put((MMINTEGER)y * stride + b0, b1 - b0 + 1, mmb_rowb);
}

static unsigned char mmb_rowpx[640];	/* one staged destination row  */
static unsigned char mmb_srcpx[640];	/* one staged source row (BLIT) */

/* ---- BLIT READ ------------------------------------------------------ */

MMG_FN void mmb_blit_read(MMINTEGER bn, MMINTEGER xi, MMINTEGER yi,
			  MMINTEGER wi, MMINTEGER hi)
{
	int stride = 0, bpp = 0, hres, vres, headless, i;
	int x = (int)xi, y = (int)yi, w = (int)wi, h = (int)hi;

	if (bn < 1 || bn > MMB_NBLIT)
		MM_RAISE("Invalid blit buffer number");
	headless = mmb_geom(&stride, &bpp, &hres, &vres);
	if (w < 1 || h < 1)
		return;
	/* cmd_blit READ clips to the screen and keeps the clipped size */
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > hres) w = hres - x;
	if (y + h > vres) h = vres - y;
	if (w < 1 || h < 1)
		return;
	if (mmb_bb[bn - 1].px != NULL)
		MM_RAISE("Buffer in use");
	mmb_bb[bn - 1].px = (unsigned char *)malloc((size_t)w * h);
	if (mmb_bb[bn - 1].px == NULL)
		MM_RAISE("Not enough memory");	/* GetMemory's own error */
	memset(mmb_bb[bn - 1].px, 0, (size_t)w * h);
	mmb_bb[bn - 1].w = (short)w;
	mmb_bb[bn - 1].h = (short)h;
	mmb_bb[bn - 1].bpp = (signed char)(headless ? 0 : bpp);
	if (headless)
		return;
	for (i = 0; i < h; i++)
		mmb_row_get(y + i, x, w, stride, bpp,
			    mmb_bb[bn - 1].px + (size_t)i * w);
}

/* ---- BLIT WRITE -----------------------------------------------------
 * One unified path for modes 0-7: the reference's mode-0 branch is an
 * optimisation of the same result, and its expand-flip-compact dance
 * becomes index arithmetic on a byte-per-pixel buffer.
 *   bit 0  mirror left/right      bit 1  mirror top/bottom
 *   bit 2  don't copy "black" (native index 0)                       */

MMG_FN void mmb_blit_write(MMINTEGER bn, MMINTEGER xi, MMINTEGER yi,
			   MMINTEGER mode)
{
	int stride = 0, bpp = 0, hres, vres, headless;
	int x1 = (int)xi, y1 = (int)yi, w, h;
	int sx0, sy0, dx0, dy0, dw, dh, i, j;
	unsigned char *B;

	if (bn < 1 || bn > MMB_NBLIT)
		MM_RAISE("Invalid blit buffer number");
	if (mmb_bb[bn - 1].px == NULL)
		MM_RAISE("Buffer not in use");
	if (mode < 0 || mode > 7)
		MM_RAISE("Invalid blit mode");
	headless = mmb_geom(&stride, &bpp, &hres, &vres);
	w = mmb_bb[bn - 1].w;
	h = mmb_bb[bn - 1].h;
	/* getint's hard range in the reference: x in -w+1..HRes */
	if (x1 < -w + 1 || x1 > hres || y1 < -h + 1 || y1 > vres)
		MM_RAISE("Invalid blit coordinates");
	if (headless)
		return;
	if (mmb_bb[bn - 1].bpp != bpp)
		MM_RAISE("Invalid blit buffer for this mode");
	if (x1 >= hres || x1 + w < 0 || y1 >= vres || y1 + h < 0)
		return;

	sx0 = x1 < 0 ? -x1 : 0;	/* first buffer column drawn (flipped space) */
	sy0 = y1 < 0 ? -y1 : 0;
	dx0 = x1 < 0 ? 0 : x1;
	dy0 = y1 < 0 ? 0 : y1;
	dw = w - sx0;
	dh = h - sy0;
	if (dx0 + dw > hres) dw = hres - dx0;
	if (dy0 + dh > vres) dh = vres - dy0;
	if (dw < 1 || dh < 1)
		return;

	B = mmb_bb[bn - 1].px;
	for (i = 0; i < dh; i++) {
		int br = sy0 + i;

		if (mode & 2)
			br = h - 1 - br;
		if (mode & 4) {
			if (mmb_row_get(dy0 + i, dx0, dw, stride, bpp,
					mmb_rowpx) < 0)
				return;
		}
		for (j = 0; j < dw; j++) {
			int bc = sx0 + j;
			unsigned char c;

			if (mode & 1)
				bc = w - 1 - bc;
			c = B[(size_t)br * w + bc];
			if (mode & 4) {
				if (c)
					mmb_rowpx[j] = c;
			} else
				mmb_rowpx[j] = c;
		}
		if (mmb_row_put(dy0 + i, dx0, dw, stride, bpp, mmb_rowpx) < 0)
			return;
	}
}

/* ---- BLIT CLOSE ----------------------------------------------------- */

MMG_FN void mmb_blit_close(MMINTEGER bn)
{
	if (bn < 1 || bn > MMB_NBLIT)
		MM_RAISE("Invalid blit buffer number");
	if (mmb_bb[bn - 1].px == NULL)
		MM_RAISE("Buffer not in use");
	free(mmb_bb[bn - 1].px);
	mmb_bb[bn - 1].px = NULL;
}

/* ---- plain BLIT: screen to screen -----------------------------------
 * blit121_self's clip of both rectangles, then rows walked in the safe
 * direction; within a row the staging buffers make horizontal overlap
 * safe, so the memmove/reverse-copy machinery of the reference reduces
 * to a direction choice. */

MMG_FN void mmb_blit_copy(MMINTEGER x1i, MMINTEGER y1i, MMINTEGER x2i,
			  MMINTEGER y2i, MMINTEGER wi, MMINTEGER hi)
{
	int stride = 0, bpp = 0, hres, vres;
	int xs = (int)x1i, ys = (int)y1i, xd = (int)x2i, yd = (int)y2i;
	int w = (int)wi, h = (int)hi, i, r0, r1, rstep;

	if (mmb_geom(&stride, &bpp, &hres, &vres))
		return;
	/* clip source, shifting the destination with it */
	if (xs < 0) { w += xs; xd -= xs; xs = 0; }
	if (ys < 0) { h += ys; yd -= ys; ys = 0; }
	if (xs + w > hres) w = hres - xs;
	if (ys + h > vres) h = vres - ys;
	/* clip destination, shifting the source with it */
	if (xd < 0) { w += xd; xs -= xd; xd = 0; }
	if (yd < 0) { h += yd; ys -= yd; yd = 0; }
	if (xd + w > hres) w = hres - xd;
	if (yd + h > vres) h = vres - yd;
	if (w < 1 || h < 1)
		return;

	if (yd > ys) { r0 = h - 1; r1 = -1; rstep = -1; }
	else	     { r0 = 0;     r1 = h;  rstep = 1;  }
	for (i = r0; i != r1; i += rstep) {
		if (mmb_row_get(ys + i, xs, w, stride, bpp, mmb_srcpx) < 0)
			return;
		if (mmb_row_put(yd + i, xd, w, stride, bpp, mmb_srcpx) < 0)
			return;
	}
}

/* ---- the two nibble decoders ----------------------------------------
 * Transcribed with their static state and their quirks intact - the RLE
 * count of 0 wraps the uint8_t and yields a run of 256, exactly as the
 * reference's available-- does.  Input nibble order is the SOURCE
 * FORMAT, fixed by the reference: raw streams are low nibble first, RLE
 * bytes are count-low / colour-high. */

static unsigned char *mmb_ncp;		/* stream cursor */
static int mmb_unc_tog;
static unsigned char mmb_rle_avail, mmb_rle_out;

MMG_FN unsigned char mmb_unc(int reset)
{
	unsigned char r;

	if (reset) {
		mmb_unc_tog = 0;
		return 0;
	}
	if (!mmb_unc_tog) {
		mmb_unc_tog = 1;
		return *mmb_ncp & 0x0F;
	}
	mmb_unc_tog = 0;
	r = (*mmb_ncp & 0xF0) >> 4;
	mmb_ncp++;
	return r;
}

MMG_FN unsigned char mmb_rle(int reset)
{
	if (reset)
		mmb_rle_avail = 0;
	if (mmb_rle_avail == 0) {
		mmb_rle_avail = *mmb_ncp & 0x0F;
		mmb_rle_out = *mmb_ncp >> 4;
		mmb_ncp++;
	}
	if (!reset)
		mmb_rle_avail--;
	return mmb_rle_out;
}

/* The shared body of COMPRESSED and MEMORY: w x h pixels from whichever
 * decoder, drawn at x1,y1 honouring the transparent index, rows outside
 * the screen consumed but not drawn - docompressed's general path, with
 * the byte stores handed to the row workhorses. */
MMG_FN void mmb_blit_decode(unsigned char (*next)(int), int x1, int y1,
			    int w, int h, int blank)
{
	int stride = 0, bpp = 0, hres, vres, headless;
	int y, x, vx0, vw;

	headless = mmb_geom(&stride, &bpp, &hres, &vres);
	next(1);
	vx0 = x1 < 0 ? 0 : x1;
	vw = (x1 + w > hres ? hres : x1 + w) - vx0;
	for (y = y1; y < y1 + h; y++) {
		int visible = !headless && y >= 0 && y < vres && vw > 0;

		if (visible &&
		    mmb_row_get(y, vx0, vw, stride, bpp, mmb_rowpx) < 0)
			visible = 0;
		for (x = x1; x < x1 + w; x++) {
			unsigned char c = next(0);

			if (!visible || x < vx0 || x >= vx0 + vw)
				continue;
			if (blank >= 0 && c == (unsigned char)blank)
				continue;
			mmb_rowpx[x - vx0] = c;
		}
		if (visible)
			mmb_row_put(y, vx0, vw, stride, bpp, mmb_rowpx);
	}
}

MMG_FN void mmb_blit_comp(MMINTEGER addr, MMINTEGER xi, MMINTEGER yi,
			  MMINTEGER blank)
{
	unsigned short *size = (unsigned short *)(long)addr;
	int w = size[0] & 0x7FFF, h = size[1] & 0x7FFF;

	if (blank < -1 || blank > 15)
		MM_RAISE("Invalid transparent colour");
	mmb_ncp = (unsigned char *)(size + 2);
	mmb_blit_decode(mmb_rle, (int)xi, (int)yi, w, h, (int)blank);
}

MMG_FN void mmb_blit_mem(MMINTEGER addr, MMINTEGER xi, MMINTEGER yi,
			 MMINTEGER blank)
{
	unsigned short *size = (unsigned short *)(long)addr;
	int w = size[0] & 0x7FFF, h = size[1] & 0x7FFF;

	if (blank < -1 || blank > 15)
		MM_RAISE("Invalid transparent colour");
	mmb_ncp = (unsigned char *)(size + 2);
	if ((size[0] & 0x8000) || (size[1] & 0x8000))
		mmb_blit_decode(mmb_rle, (int)xi, (int)yi, w, h, (int)blank);
	else
		mmb_blit_decode(mmb_unc, (int)xi, (int)yi, w, h, (int)blank);
}

#endif /* MMB_BLIT_H */

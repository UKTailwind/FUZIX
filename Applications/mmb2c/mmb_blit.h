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

/* ---- the window: many rows per crossing ------------------------------
 *
 * Every rectangle in this file and in mmb_sprite.h is a loop of the two
 * row calls below, and each of those is a SYSTEM CALL - two for a
 * write, which read-modify-writes for the boundary bytes.  A 9x9 sprite
 * cost about fifty crossings to show; the board measured 0.54ms per
 * SPRITE SHOW, and 91% of brownian.bas's 38ms frame was in SPRITE SHOW
 * alone.  MMBasic runs the same algorithm as plain memory access in one
 * address space, which is the whole of the difference.
 *
 * So: a caller opens a window over the rectangle it is about to walk,
 * and the row calls serve from a batch of rows fetched in ONE crossing
 * instead of going to the kernel each time.  Nothing else changes -
 * same arguments, same pixels, same order - which is why every caller
 * gets it for two lines and none of the pixel logic moves.
 *
 * A row outside the open window, or no window at all, takes the direct
 * path exactly as before: the window is an optimisation, never a
 * precondition, and a caller that forgets to close one loses nothing
 * but the batching.
 */
#ifndef MMB_WINB
#define MMB_WINB 1024			/* packed bytes held at once */
#endif

static unsigned char mmb_winbuf[MMB_WINB];
static struct {
	int open;			/* a window is being walked      */
	int y0, y1;			/* row range it covers           */
	int b0, len;			/* byte span within a row        */
	int stride;
	int rows;			/* rows per batch                */
	int have;			/* rows resident, 0 = none       */
	int base;			/* first resident row            */
	int dirty;			/* resident rows need writing    */
	int fault;			/* a transfer failed - stop      */
} mmb_win;

/* Push the resident batch back if anything changed. */
MMG_FN int mmb_win_flush(void)
{
	int r = 0;

	if (mmb_win.dirty && mmb_win.have > 0) {
		if (mm_fb_putr((MMINTEGER)mmb_win.base * mmb_win.stride
			       + mmb_win.b0, mmb_win.len, mmb_win.have,
			       mmb_win.stride, mmb_winbuf) < 0) {
			mmb_win.fault = 1;
			r = -1;
		}
	}
	mmb_win.dirty = 0;
	return r;
}

/*	Open a window over rows [y0, y0+h) of the byte span the caller is
 *	about to walk.  x0/w are PIXELS and are turned into the same byte
 *	span the row calls compute, so a row inside the window is served
 *	from the batch and one outside is not. */
MMG_FN void mmb_win_open(int y0, int h, int x0, int w, int stride, int bpp)
{
	int b0, b1, per;

	mmb_win.open = 0;
	mmb_win.have = 0;
	mmb_win.dirty = 0;
	mmb_win.fault = 0;
	if (h < 1 || w < 1 || stride < 1)
		return;
	if (bpp == 4) {
		b0 = x0 >> 1;
		b1 = (x0 + w - 1) >> 1;
	} else if (bpp == 1) {
		b0 = x0 >> 3;
		b1 = (x0 + w - 1) >> 3;
	} else
		return;
	mmb_win.len = b1 - b0 + 1;
	if (mmb_win.len > MMB_WINB)
		return;			/* a row wider than the batch */
	per = MMB_WINB / mmb_win.len;
	if (per < 2)
		return;			/* one row a batch buys nothing */
	if (per > h)
		per = h;
	mmb_win.y0 = y0;
	mmb_win.y1 = y0 + h;
	mmb_win.b0 = b0;
	mmb_win.stride = stride;
	mmb_win.rows = per;
	mmb_win.base = 0;
	mmb_win.open = 1;
}

MMG_FN int mmb_win_close(void)
{
	int r = mmb_win.open ? mmb_win_flush() : 0;

	mmb_win.open = 0;
	mmb_win.have = 0;
	return r;
}

/*	The batch holding row y, fetched if it is not the resident one.
 *	NULL means "not served here" - the caller does it the direct way. */
MMG_FN unsigned char *mmb_win_bytes(int y, int b0, int len)
{
	int base;

	if (!mmb_win.open || mmb_win.fault)
		return 0;
	if (y < mmb_win.y0 || y >= mmb_win.y1)
		return 0;
	if (b0 != mmb_win.b0 || len != mmb_win.len)
		return 0;		/* a different span: not ours */
	base = mmb_win.y0
	     + ((y - mmb_win.y0) / mmb_win.rows) * mmb_win.rows;
	if (mmb_win.have == 0 || base != mmb_win.base) {
		int n = mmb_win.y1 - base;

		if (mmb_win_flush() < 0)
			return 0;
		if (n > mmb_win.rows)
			n = mmb_win.rows;
		if (mm_fb_readr((MMINTEGER)base * mmb_win.stride + mmb_win.b0,
				mmb_win.len, n, mmb_win.stride,
				mmb_winbuf) < 0) {
			mmb_win.fault = 1;
			mmb_win.have = 0;
			return 0;
		}
		mmb_win.base = base;
		mmb_win.have = n;
	}
	return mmb_winbuf + (size_t)(y - mmb_win.base) * mmb_win.len;
}

MMG_FN int mmb_row_get(int y, int x0, int w, int stride, int bpp,
		       unsigned char *px)
{
	int b0, b1, i;
	unsigned char *row;

	if (bpp == 4) {
		b0 = x0 >> 1;
		b1 = (x0 + w - 1) >> 1;
	} else {
		b0 = x0 >> 3;
		b1 = (x0 + w - 1) >> 3;
	}
	/* From the open window if this row is in it - see mmb_win_open.
	   Otherwise the direct crossing, exactly as before. */
	row = mmb_win_bytes(y, b0, b1 - b0 + 1);
	if (row == 0) {
		if (mm_fb_read((MMINTEGER)y * stride + b0, b1 - b0 + 1,
			       mmb_rowb) < 0)
			return -1;
		row = mmb_rowb;
	}
	for (i = 0; i < w; i++) {
		int x = x0 + i;

		if (bpp == 4)
			px[i] = (x & 1) ? (row[(x >> 1) - b0] & 15)
					: (row[(x >> 1) - b0] >> 4);
		else
			px[i] = (row[(x >> 3) - b0] >> (7 - (x & 7))) & 1;
	}
	return 0;
}

MMG_FN int mmb_row_put(int y, int x0, int w, int stride, int bpp,
		       const unsigned char *px)
{
	int b0, b1, i, win = 0;
	unsigned char *row;

	if (bpp == 4) {
		b0 = x0 >> 1;
		b1 = (x0 + w - 1) >> 1;
	} else {
		b0 = x0 >> 3;
		b1 = (x0 + w - 1) >> 3;
	}
	/* Read-modify-write, always: the boundary bytes carry pixels that
	 * are not ours, and one uniform path beats reasoning per case
	 * about which edges align (the pixel-batch lesson applied).
	 *
	 * Inside an open window the read is already done - the batch holds
	 * these bytes - so the modify happens in place and the write back
	 * is one crossing for the whole batch, not two per row. */
	row = mmb_win_bytes(y, b0, b1 - b0 + 1);
	if (row == 0) {
		if (mm_fb_read((MMINTEGER)y * stride + b0, b1 - b0 + 1,
			       mmb_rowb) < 0)
			return -1;
		row = mmb_rowb;
	} else
		win = 1;
	for (i = 0; i < w; i++) {
		int x = x0 + i;

		if (bpp == 4) {
			unsigned char *p = &row[(x >> 1) - b0];

			if (x & 1)
				*p = (*p & 0xF0) | (px[i] & 15);
			else
				*p = (*p & 0x0F) | ((px[i] & 15) << 4);
		} else {
			unsigned char *p = &row[(x >> 3) - b0];
			unsigned char m = 0x80 >> (x & 7);

			if (px[i])
				*p |= m;
			else
				*p &= (unsigned char)~m;
		}
	}
	if (win) {
		mmb_win.dirty = 1;
		return 0;
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

/* ---- BLIT LOAD / BLIT LOADBMP ---------------------------------------
 *
 *	BLIT LOAD [#]n, f$ [, x [, y [, w [, h]]]]
 *
 *	The reference takes both spellings for the one command, and this
 *	is SPRITE LOADBMP filling a blit buffer instead of a sprite: the
 *	decoding is /usr/bin/loadimage's, in another process, and the
 *	picture comes back down a pipe.  See mms_loadbmp in mmb_sprite.h
 *	for the protocol; the two are deliberately the same shape.
 *
 *	ONE INDEX PER BYTE, as every blit buffer here is.  The reference's
 *	blit form on an LCD PicoMite keeps RGB888 - three bytes a pixel -
 *	because that display's buffers are colour; ours are the screen's
 *	own RGB121 indices, so this stores what BLIT WRITE will draw.
 *
 *	The buffer records the CURRENT screen depth, exactly as BLIT READ
 *	does, because BLIT WRITE refuses a buffer from another mode.  A
 *	picture loaded in MODE 2 and written in MODE 1 is not a silent
 *	half-drawing; it is "Invalid blit buffer for this mode".
 *
 *	Unlike a sprite it is NOT bounded by the screen: a blit buffer is
 *	a rectangle of memory, the reference does not bound it either, and
 *	malloc says when it is too big.
 */
MMG_FN void mmb_blit_loadbmp(MMINTEGER bn, const char *file, MMINTEGER xo,
			     MMINTEGER yo, MMINTEGER wi, MMINTEGER hi)
{
	int stride = 0, bpp = 0, hres, vres, headless;
	unsigned char hdr[4];
	int fd, w, h, got, n;

	if (bn < 1 || bn > MMB_NBLIT)
		MM_RAISE("Invalid blit buffer number");
	if (mmb_bb[bn - 1].px != NULL)
		MM_RAISE("Buffer in use");
	if (xo < 0 || yo < 0)
		MM_RAISE("Coordinates");
	headless = mmb_geom(&stride, &bpp, &hres, &vres);

	mm_run_begin();
	mm_run_arg("\011loadimage");
	mm_run_arg("\002-s");
	mm_run_arg(file);
	mm_run_arg_i(xo);
	mm_run_arg_i(yo);
	mm_run_arg_i(wi);
	mm_run_arg_i(hi);
	fd = mm_run_pipe();
	if (fd < 0)
		return;			/* mm_run_pipe raised it */

	got = 0;
	while (got < 4) {
		n = (int)mm_run_pipe_read(fd, hdr + got, 4 - got);
		if (n <= 0)
			break;
		got += n;
	}
	if (got < 4) {
		if (mm_run_pipe_close(fd) < 0)
			return;
		MM_RAISE("The BMP could not be decoded");
	}
	w = hdr[0] | (hdr[1] << 8);
	h = hdr[2] | (hdr[3] << 8);
	if (w < 1 || h < 1) {
		mm_run_pipe_close(fd);
		MM_RAISE("Coordinates");
	}
	mmb_bb[bn - 1].px = (unsigned char *)malloc((size_t)w * h);
	if (mmb_bb[bn - 1].px == NULL) {
		mm_run_pipe_close(fd);
		MM_RAISE("Not enough memory");	/* GetMemory's own error */
	}
	got = 0;
	while (got < w * h) {
		n = (int)mm_run_pipe_read(fd, mmb_bb[bn - 1].px + got,
					  w * h - got);
		if (n <= 0)
			break;
		got += n;
	}
	if (got < w * h) {
		/*	A half-filled buffer must not be left looking like
		 *	a good one - and the decoder's own message is the
		 *	useful one, so close first. */
		free(mmb_bb[bn - 1].px);
		mmb_bb[bn - 1].px = NULL;
		if (mm_run_pipe_close(fd) < 0)
			return;
		MM_RAISE("The BMP could not be decoded");
	}
	mmb_bb[bn - 1].w = (short)w;
	mmb_bb[bn - 1].h = (short)h;
	mmb_bb[bn - 1].bpp = (signed char)(headless ? 0 : bpp);
	if (mm_run_pipe_close(fd) < 0) {
		free(mmb_bb[bn - 1].px);
		mmb_bb[bn - 1].px = NULL;
		return;			/* a failed decoder is an error */
	}
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

/*	One source row, staged PACKED - low nibble the left pixel, which
 *	is the source's own order.
 *
 *	The decoder is pulled exactly w times whether the row is drawn or
 *	not: an RLE run carries across rows, so consuming the stream is
 *	not optional, and a clipped row must still advance it.
 */
/*	Staged rows are the SOURCE's full width, not the clipped width -
 *	an RLE run has to be consumed whether it lands on screen or not -
 *	so the capacity is a hard limit on w and not a soft one.  A source
 *	wider than this takes the original per-pixel path, which stages
 *	nothing and is bounded by the screen instead.  Getting this wrong
 *	is a bss overrun from a number read out of a sprite header, which
 *	is exactly the kind of thing a bad address turns into a crash. */
#define MMB_PACKPX 640			/* pixels a staged row may hold */
static unsigned char mmb_packrow[MMB_PACKPX / 2];

MMG_FN void mmb_pack_row(unsigned char (*next)(int), int w)
{
	int i, n = w >> 1;

	for (i = 0; i < n; i++) {
		unsigned char lo = next(0);

		mmb_packrow[i] = (unsigned char)(lo | (next(0) << 4));
	}
	if (w & 1)
		mmb_packrow[n] = next(0);
}

/*	The same row, taken a RUN at a time instead of a pixel at a time.
 *
 *	mmb_rle hands back one pixel per call and the caller cannot see
 *	that forty of them are the same; this reads the run structure
 *	directly and fills.  Sprite sheets are mostly flat colour, so the
 *	runs are long and this is where the coding pays.
 *
 *	The count quirk is preserved exactly: a stored count of 0 means
 *	256, because the reference's `available--` wraps a uint8_t and
 *	mmb_rle reproduces that one pixel at a time.
 */
MMG_FN void mmb_pack_row_rle(int w)
{
	int i = 0;

	while (i < w) {
		int run, take, j;
		unsigned char c;

		if (mmb_rle_avail == 0) {
			mmb_rle_avail = *mmb_ncp & 0x0F;
			mmb_rle_out = (unsigned char)(*mmb_ncp >> 4);
			mmb_ncp++;
			if (mmb_rle_avail == 0)
				run = 256;	/* the wrap, spelled out */
			else
				run = mmb_rle_avail;
		} else
			run = mmb_rle_avail;
		take = (run > w - i) ? w - i : run;
		c = mmb_rle_out;
		for (j = 0; j < take; j++, i++) {
			if (i & 1)
				mmb_packrow[i >> 1] =
					(unsigned char)(mmb_packrow[i >> 1]
							| (c << 4));
			else
				mmb_packrow[i >> 1] = c;
		}
		mmb_rle_avail = (unsigned char)(run - take);
	}
}

/*	Merge a staged row into the destination's packed bytes.
 *
 *	Everything here is byte work on the row the window already holds
 *	in SRAM: no expansion, no second pass, and no crossing - the
 *	window flushes the whole rectangle in one when it closes.
 */
MMG_FN void mmb_merge_row(const unsigned char *sp, int y, int x1, int w,
			  int vx0, int vw, int stride, int blank)
{
	int b0 = vx0 >> 1, b1 = (vx0 + vw - 1) >> 1;
	int nb = b1 - b0 + 1, i, win = 1;
	unsigned char *row = mmb_win_bytes(y, b0, nb);

	if (row == 0) {
		if (mm_fb_read((MMINTEGER)y * stride + b0, nb, mmb_rowb) < 0)
			return;
		row = mmb_rowb;
		win = 0;
	}
	/*	The aligned, opaque case - a tile drawn on an even column,
	 *	nothing clipped - is the one games actually use, and it
	 *	reduces to swapping the nibbles of each source byte: the
	 *	source has the left pixel low, the framebuffer has it high.
	 */
	if (blank < 0 && !(x1 & 1) && vx0 == x1 && vw == w) {
		int n = w >> 1;

		for (i = 0; i < n; i++) {
			unsigned char s = sp[i];

			row[i] = (unsigned char)((s << 4) | (s >> 4));
		}
		if (w & 1)
			row[n] = (unsigned char)((row[n] & 0x0F)
						 | (sp[n] << 4));
		if (win)
			mmb_win.dirty = 1;
		else
			mm_fb_put((MMINTEGER)y * stride + b0, nb, mmb_rowb);
		return;
	}
	/*	The general case: two pixels an iteration, and a byte whose
	 *	pixels are both transparent costs one compare and no store.
	 */
	for (i = 0; i < w; i += 2) {
		unsigned char s = sp[i >> 1];
		int lo = s & 15, hi = (s >> 4) & 15;
		int k;

		if (blank >= 0 && lo == blank && hi == blank
		    && i + 1 < w)
			continue;
		for (k = 0; k < 2 && i + k < w; k++) {
			int c = k ? hi : lo;
			int x = x1 + i + k;
			unsigned char *p;

			if (blank >= 0 && c == blank)
				continue;
			if (x < vx0 || x >= vx0 + vw)
				continue;
			p = &row[(x >> 1) - b0];
			if (x & 1)
				*p = (unsigned char)((*p & 0xF0) | c);
			else
				*p = (unsigned char)((*p & 0x0F) | (c << 4));
		}
	}
	if (win)
		mmb_win.dirty = 1;
	else
		mm_fb_put((MMINTEGER)y * stride + b0, nb, mmb_rowb);
}

/* The shared body of COMPRESSED and MEMORY: w x h pixels from whichever
 * decoder, drawn at x1,y1 honouring the transparent index, rows outside
 * the screen consumed but not drawn - docompressed's general path, with
 * the byte stores handed to the row workhorses. */
MMG_FN void mmb_blit_decode(unsigned char (*next)(int), int x1, int y1,
			    int w, int h, int blank)
{
	int stride = 0, bpp = 0, hres, vres, headless;
	int y, x, vx0, vw, vy0, vy1;

	headless = mmb_geom(&stride, &bpp, &hres, &vres);
	next(1);
	vx0 = x1 < 0 ? 0 : x1;
	vw = (x1 + w > hres ? hres : x1 + w) - vx0;
	/*
	 *	One crossing for the whole sprite, not two per row.
	 *
	 *	This is the batching mmb_win_open was written for and the
	 *	blitter was simply never given: a 24x24 tile is 12 packed
	 *	bytes a row, so the entire tile fits one batch and its 48
	 *	crossings become 2.  PETSCII Robots redraws 77 tiles for a
	 *	step - 3,696 crossings, which is seconds - and MMBasic does
	 *	the identical algorithm as plain memory writes in one
	 *	address space.  That difference, not the decoding, is what
	 *	made a game an RP2040 interprets comfortably crawl here.
	 */
	vy0 = y1 < 0 ? 0 : y1;
	vy1 = y1 + h > vres ? vres : y1 + h;
	if (!headless)
		mmb_win_open(vy0, vy1 - vy0, vx0, vw, stride, bpp);
	for (y = y1; y < y1 + h; y++) {
		int visible = !headless && y >= 0 && y < vres && vw > 0;

		/*	NIBBLE TO NIBBLE, at 4bpp.
		 *
		 *	Source and destination are BOTH packed two pixels to
		 *	a byte, so expanding a row to one byte per pixel,
		 *	editing it and squeezing it back touches every pixel
		 *	three times to move it once - and the pull was a call
		 *	through a function pointer per pixel on top.  The
		 *	board measured that at 1.35us a pixel, ~500 cycles,
		 *	while the kernel does the same work on the same PSRAM
		 *	at a sixth of it.
		 *
		 *	So the row is staged packed and merged a BYTE at a
		 *	time: two pixels an iteration, a whole byte skipped
		 *	when both its pixels are transparent, and for the
		 *	aligned case - even x, even width, opaque - the merge
		 *	is a nibble swap with no per-pixel work at all.
		 *
		 *	1bpp keeps the original path: there a byte is eight
		 *	pixels, none of this applies, and the modes that use
		 *	it are not what sprites are drawn in.
		 */
		if (bpp == 4 && w <= MMB_PACKPX) {
			const unsigned char *src;

			/*	An UNCOMPRESSED source of even width is
			 *	already exactly the staged row - same
			 *	packing, same order, row-aligned - so it is
			 *	used where it lies and not a single pixel is
			 *	pulled.  (Odd widths straddle a byte from one
			 *	row to the next, so those still stage.)
			 */
			if (next == mmb_unc && !(w & 1)) {
				src = mmb_ncp;
				mmb_ncp += w >> 1;
			} else {
				if (next == mmb_rle)
					mmb_pack_row_rle(w);
				else
					mmb_pack_row(next, w);
				src = mmb_packrow;
			}
			if (visible)
				mmb_merge_row(src, y, x1, w, vx0, vw,
					      stride, blank);
			continue;
		}
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
	mmb_win_close();
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

/* ---- BLIT FRAMEBUFFER src, dst, x1, y1, x2, y2, w, h [, t] ----------
 * A rectangle from one of N/F/L to another, through the per-process
 * draw target: select the source, read the rows, select the
 * destination, write them, put the program's own target back.
 * mm_fb_write does the "is it created" checks - the same errors the
 * FRAMEBUFFER statements raise.  The reference refuses mode 1
 * (Blit.c:799) and so does this.  Source rectangle must lie inside the
 * screen; the destination clips, as blit121 clips. */

MMG_FN void mmb_blit_fb(MMINTEGER src, MMINTEGER dst,
			MMINTEGER x1i, MMINTEGER y1i,
			MMINTEGER x2i, MMINTEGER y2i,
			MMINTEGER wi, MMINTEGER hi, MMINTEGER blank)
{
	int stride = 0, bpp = 0, hres, vres, headless;
	int x1 = (int)x1i, y1 = (int)y1i, x2 = (int)x2i, y2 = (int)y2i;
	int w = (int)wi, h = (int)hi;
	int keep, sx0, dx0, dy0, dw, dh, i, j;

	if (blank < -1 || blank > 15)
		MM_RAISE("Invalid transparent colour");
	headless = mmb_geom(&stride, &bpp, &hres, &vres);
	if (!headless && bpp != 4)
		MM_RAISE("Not available in mode 1");
	if (x1 < 0 || y1 < 0 || w < 1 || h < 1 ||
	    x1 + w > hres || y1 + h > vres)
		MM_RAISE("Invalid coordinates");
	if (headless)
		return;

	sx0 = x2 < 0 ? -x2 : 0;
	dx0 = x2 < 0 ? 0 : x2;
	dy0 = y2 < 0 ? 0 : y2;
	dw = w - sx0;
	dh = h - (y2 < 0 ? -y2 : 0);
	if (dx0 + dw > hres) dw = hres - dx0;
	if (dy0 + dh > vres) dh = vres - dy0;
	if (dw < 1 || dh < 1)
		return;

	keep = (int)mm_fb_cur();
	for (i = 0; i < dh; i++) {
		int sy = y1 + (y2 < 0 ? -y2 : 0) + i;

		mm_fb_write(src);
		if (mmb_row_get(sy, x1 + sx0, dw, stride, bpp,
				mmb_srcpx) < 0)
			break;
		mm_fb_write(dst);
		if (blank >= 0) {
			if (mmb_row_get(dy0 + i, dx0, dw, stride, bpp,
					mmb_rowpx) < 0)
				break;
			for (j = 0; j < dw; j++)
				if (mmb_srcpx[j] != (unsigned char)blank)
					mmb_rowpx[j] = mmb_srcpx[j];
			if (mmb_row_put(dy0 + i, dx0, dw, stride, bpp,
					mmb_rowpx) < 0)
				break;
		} else if (mmb_row_put(dy0 + i, dx0, dw, stride, bpp,
				       mmb_srcpx) < 0)
			break;
	}
	mm_fb_write(keep);
}

/* ---- BLIT FLASH n, dst, x1, y1, x2, y2, w, h [, t] ------------------
 * An image out of a pseudo flash slot (mmb_flash.h) onto N/F/L.  The
 * slot layout is the REFERENCE's: uint32 width, uint32 height, then
 * packed 4bpp with the LOW nibble the left pixel - the PicoMite's
 * packing, not this machine's - so asset files made for a PicoMite
 * work unmodified.  In mode 1 a non-zero index becomes ink, the same
 * rule every other decoder here follows.  Only compiled when the
 * program also uses a FLASH command, which is what the guard means. */

#ifdef MMB_FLASH_H
MMG_FN void mmb_blit_flash(MMINTEGER n, MMINTEGER dst,
			   MMINTEGER x1i, MMINTEGER y1i,
			   MMINTEGER x2i, MMINTEGER y2i,
			   MMINTEGER wi, MMINTEGER hi, MMINTEGER blank)
{
	int stride = 0, bpp = 0, hres, vres, headless;
	int x1 = (int)x1i, y1 = (int)y1i, x2 = (int)x2i, y2 = (int)y2i;
	int w = (int)wi, h = (int)hi;
	int keep, sx0, dx0, dy0, dw, dh, i, j, sstride;
	unsigned long hs, vs;
	unsigned char *s = mmf_addr(n);

	if (s == NULL)
		return;
	if (blank < -1 || blank > 15)
		MM_RAISE("Invalid transparent colour");
	hs = (unsigned long)s[0] | ((unsigned long)s[1] << 8) |
	     ((unsigned long)s[2] << 16) | ((unsigned long)s[3] << 24);
	vs = (unsigned long)s[4] | ((unsigned long)s[5] << 8) |
	     ((unsigned long)s[6] << 16) | ((unsigned long)s[7] << 24);
	if (hs > 3840 || vs > 2160)
		MM_RAISE("Invalid Image");
	if (x1 < 0 || y1 < 0 || w < 1 || h < 1 ||
	    (unsigned long)(x1 + w) > hs || (unsigned long)(y1 + h) > vs)
		MM_RAISE("Invalid coordinates");
	headless = mmb_geom(&stride, &bpp, &hres, &vres);
	if (headless)
		return;

	sx0 = x2 < 0 ? -x2 : 0;
	dx0 = x2 < 0 ? 0 : x2;
	dy0 = y2 < 0 ? 0 : y2;
	dw = w - sx0;
	dh = h - (y2 < 0 ? -y2 : 0);
	if (dx0 + dw > hres) dw = hres - dx0;
	if (dy0 + dh > vres) dh = vres - dy0;
	if (dw < 1 || dh < 1)
		return;

	sstride = ((int)hs + 1) >> 1;
	keep = (int)mm_fb_cur();
	mm_fb_write(dst);
	for (i = 0; i < dh; i++) {
		const unsigned char *row = s + 8 +
		    (long)(y1 + (y2 < 0 ? -y2 : 0) + i) * sstride;

		for (j = 0; j < dw; j++) {
			int sx = x1 + sx0 + j;

			/* PicoMite packing: LOW nibble = even pixel */
			mmb_srcpx[j] = (sx & 1) ? (row[sx >> 1] >> 4)
						: (row[sx >> 1] & 15);
		}
		if (blank >= 0) {
			if (mmb_row_get(dy0 + i, dx0, dw, stride, bpp,
					mmb_rowpx) < 0)
				break;
			for (j = 0; j < dw; j++)
				if (mmb_srcpx[j] != (unsigned char)blank)
					mmb_rowpx[j] = mmb_srcpx[j];
			if (mmb_row_put(dy0 + i, dx0, dw, stride, bpp,
					mmb_rowpx) < 0)
				break;
		} else if (mmb_row_put(dy0 + i, dx0, dw, stride, bpp,
				       mmb_srcpx) < 0)
			break;
	}
	mm_fb_write(keep);
}
#endif /* MMB_FLASH_H */

#endif /* MMB_BLIT_H */

#ifndef MMB_GFX_FILL_H
#define MMB_GFX_FILL_H
/*
 *	FILL x, y, colour [, boundary]
 *
 *	MMBasic's cmd_fill -> floodfill (graphics/DrawFill.c).  Two modes,
 *	as there: with a boundary colour it fills everything reachable that
 *	is not that colour; without one it replaces the colour found at the
 *	starting point.
 *
 *	SCANLINE READS, which is how MMBasic does it and, on this machine,
 *	the difference between usable and not.  A pixel at a time through
 *	mm_pixel_get is a system call at 2.5us, board-measured - 800us to
 *	look at one 320-pixel row.  mm_fb_read takes the whole row in one
 *	crossing as raw bytes (GFXIOC_BLITRD, MMBasic's ReadBufferFast),
 *	160 bytes for a 4bpp mode.
 *
 *	Board-measured, once it worked: a circle interior 6ms, a whole
 *	320x240 screen 75ms.  The pixel-at-a-time version would have been
 *	about 230ms for the screen, so this is three times faster and not
 *	the hundred the syscall arithmetic suggests - because with the
 *	reads that cheap the walk is bound by the WRITES and the span
 *	bookkeeping instead.  Worth writing down: the read was still the
 *	right thing to fix, and it is no longer the thing to fix next.
 *
 *	So the comparisons here are on NATIVE INDICES, not RGB888: that is
 *	what is in the bytes.  mm_colour_index turns the caller's colours
 *	into indices once, up front, because the palette and the
 *	nearest-match belong to the kernel and a program scanning bytes
 *	cannot work them out.
 *
 *	The walk is span-based: find the whole run on a row, fill it, then
 *	probe the rows above and below across just that run.  Each row is
 *	read once per visit rather than once per pixel.
 *
 *	No visited bitmap is needed in replace mode - a filled pixel no
 *	longer matches the origin index, so it cannot be seeded twice.  In
 *	boundary mode the fill colour itself stops the walk, which is why
 *	filling with the boundary colour does nothing; MMBasic returns
 *	immediately in that case too.
 *
 *	FALLS BACK, rather than failing, where there is no raw read: the
 *	host build has no framebuffer and mm_fb_read says so, so the fill
 *	is simply not done there, the same silence every other primitive
 *	keeps when there is no display.
 */

#include "mmb_gfx_pts.h"

/*	Seed stack: one entry per span still to come back to.  Static
 *	because the program's stack is 8K.  MMBasic grows a linked list of
 *	256-entry blocks; this is capped and says so rather than filling
 *	half a shape in silence. */
#define MMG_FILL_SEEDS	512
#define MMG_FILL_MAXW	160	/* bytes: 320 pixels at 4bpp, the widest */

static short mmg_fsx[MMG_FILL_SEEDS];
static short mmg_fsy[MMG_FILL_SEEDS];
static unsigned char mmg_frow[MMG_FILL_MAXW];
static short mmg_frow_y = -1;		/* which row is in the buffer */

/* One pixel out of the raw row buffer. */
static int mmg_fill_px(int x, int bpp)
{
	if (bpp == 4)
		return (x & 1) ? (mmg_frow[x >> 1] & 15)
			       : (mmg_frow[x >> 1] >> 4);
	return (mmg_frow[x >> 3] >> (7 - (x & 7))) & 1;
}

/* Pull row y in, unless it is already there. */
static int mmg_fill_row(int y, int stride)
{
	if (mmg_frow_y == y)
		return 0;
	if (stride > MMG_FILL_MAXW)
		return -1;
	if (mm_fb_read((MMINTEGER)y * stride, stride, mmg_frow) < 0)
		return -1;
	mmg_frow_y = (short)y;
	return 0;
}

static void mmg_fill(MMINTEGER sx, MMINTEGER sy, MMINTEGER newc,
		     MMINTEGER bound)
{
	short rcs[MMG_BATCH * 4];
	int nr = 0, nseed = 0, overflow = 0;
	int hres, vres, stride, bpp, geom;
	int x, y, x1, x2, xi, dir, inside;
	int origin, newi, boundi = -1;
	int boundary_mode = (bound != MM_CUR);

	geom = (int)mm_fb_geom();
	if (geom < 0)
		return;				/* no display: the host build */
	stride = geom >> 8;
	bpp = geom & 0xFF;
	if (bpp != 1 && bpp != 4)
		return;
	hres = (int)mm_hres();
	vres = (int)mm_vres();
	if (hres <= 0 || vres <= 0)
		return;

	x = (int)sx;
	y = (int)sy;
	if (x < 0 || x >= hres || y < 0 || y >= vres)
		return;				/* MMBasic bounds-checks and returns */

	newi = (int)mm_colour_index(newc);
	if (newi < 0)
		return;
	if (boundary_mode) {
		boundi = (int)mm_colour_index(bound);
		if (boundi < 0)
			return;
	}

	mmg_frow_y = -1;			/* nothing cached yet */
	if (mmg_fill_row(y, stride) < 0)
		return;
	origin = mmg_fill_px(x, bpp);

	if (boundary_mode) {
		if (origin == boundi)
			return;			/* started on the boundary */
	} else if (origin == newi) {
		return;				/* already that colour */
	}

	mmg_fsx[nseed] = (short)x;
	mmg_fsy[nseed] = (short)y;
	nseed++;

#define MMG_FILL_OPEN(v) (boundary_mode ? ((v) != boundi && (v) != newi) \
					: ((v) == origin))

	while (nseed > 0) {
		nseed--;
		x = mmg_fsx[nseed];
		y = mmg_fsy[nseed];

		if (mmg_fill_row(y, stride) < 0)
			break;
		if (!MMG_FILL_OPEN(mmg_fill_px(x, bpp)))
			continue;

		x1 = x;
		while (x1 > 0 && MMG_FILL_OPEN(mmg_fill_px(x1 - 1, bpp)))
			x1--;
		x2 = x;
		while (x2 < hres - 1 && MMG_FILL_OPEN(mmg_fill_px(x2 + 1, bpp)))
			x2++;

		/* Probe the rows either side BEFORE drawing: the span about
		   to be filled is still open in the cached rows, and drawing
		   first would mean re-reading this row to see it closed. */
		for (dir = -1; dir <= 1; dir += 2) {
			int yy = y + dir;

			if (yy < 0 || yy >= vres)
				continue;
			if (mmg_fill_row(yy, stride) < 0)
				continue;
			inside = 0;
			for (xi = x1; xi <= x2; xi++) {
				if (MMG_FILL_OPEN(mmg_fill_px(xi, bpp))) {
					if (!inside) {
						if (nseed < MMG_FILL_SEEDS) {
							mmg_fsx[nseed] = (short)xi;
							mmg_fsy[nseed] = (short)yy;
							nseed++;
						} else {
							overflow = 1;
						}
						inside = 1;
					}
				} else {
					inside = 0;
				}
			}
		}

		/* Now draw it, and drop the cache: the row just changed. */
		mmg_rc(rcs, &nr, newc, x1, y, x2, y);
		mm_fill(rcs, nr, newc);
		nr = 0;
		mmg_frow_y = -1;
	}

#undef MMG_FILL_OPEN

	if (overflow)
		mm_error("FILL ran out of seeds - shape too complex");
}

#endif /* MMB_GFX_FILL_H */

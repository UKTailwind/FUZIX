#ifndef MMB_GFX_POLYGON_H
#define MMB_GFX_POLYGON_H
/*
 *	POLYGON n, xarray(), yarray() [, bordercolour [, fillcolour]]
 *
 *	MMBasic's cmd_polygon -> polygon() (graphics/DrawFill.c).
 *
 *	CLOSED, always.  MMBasic's polygon() takes a `close' flag and
 *	cmd_polygon passes 1; the open form belongs to the internal caller
 *	that draws a GUI element.  The edge from the last vertex back to
 *	the first is therefore drawn, and the fill assumes it.
 *
 *	n == 0 means "as many vertices as the array holds", MMBasic's
 *	xcount == 0.  Fewer than three is an error there and here.
 *
 *	THE FILL IS CROSSING-BASED, NOT MIN/MAX PER ROW.  The first
 *	version of this reused TRIANGLE's extent tables, which record only
 *	the leftmost and rightmost x on each row - and a triangle is always
 *	convex, so that is exact for it and WRONG here.  Board-tested on an
 *	arrowhead: the notch between the two arms filled solid, because
 *	min/max cannot express a gap.  So each row collects every edge
 *	crossing, sorts them, and fills between alternate pairs - the
 *	even-odd rule, which is what MMBasic's fill_end_fill does and what
 *	makes a concave or self-intersecting outline come out right.
 *
 *	The half-open test `y >= ymin(edge) && y < ymax(edge)' is what
 *	stops a vertex being counted twice where two edges meet, which
 *	would swap inside for outside for the rest of the row.
 *
 *	Coordinates arrive as MMFLOAT or MMINTEGER arrays - MMBasic takes
 *	either through a stride - so both pointers are passed and exactly
 *	one of each pair is non-NULL, the shape mm_pixels already uses.
 */

#include "mmb_gfx_pts.h"

#define MMG_POLY_MIN	3
#define MMG_POLY_MAX	9999		/* MMBasic's own ceiling */

/*	Crossings of one row.  A convex shape has two; a comb with N
 *	teeth has 2N.  Static rather than automatic because the stack is
 *	8K, and capped because a row needing more than this is a shape
 *	nobody is drawing on a 320-pixel screen.  Excess crossings are
 *	dropped, which loses spans rather than corrupting memory. */
#define MMG_POLY_XS	64

static short mmg_pxs[MMG_POLY_XS];

static int mmg_poly_xi(const MMFLOAT *xf, const MMINTEGER *xi, int i)
{
	return xf ? (int)xf[i] : (int)xi[i];
}

static int mmg_poly_yi(const MMFLOAT *yf, const MMINTEGER *yi, int i)
{
	return yf ? (int)yf[i] : (int)yi[i];
}

static void mmg_polygon(const MMFLOAT *xf, const MMINTEGER *xi,
			const MMFLOAT *yf, const MMINTEGER *yi,
			MMINTEGER n, MMINTEGER avail,
			MMINTEGER c, MMINTEGER fill)
{
	short rcs[MMG_BATCH * 4];
	int nr = 0, vres, ymin, ymax, y, i, j, count, nx, k, t;
	int ax, ay, bx, by;

	if (n == 0)
		n = avail;		/* MMBasic's xcount == 0 */
	if (n < MMG_POLY_MIN || n > MMG_POLY_MAX) {
		mm_error("Invalid number of vertices");
		return;
	}
	if (n > avail) {
		mm_error("Dimensions");
		return;
	}
	count = (int)n;

	if (fill != MM_CUR) {
		vres = (int)mm_vres();
		if (vres <= 0)
			goto outline;	/* no display: the host build */

		ymin = 1 << 30;
		ymax = -(1 << 30);
		for (i = 0; i < count; i++) {
			y = mmg_poly_yi(yf, yi, i);
			if (y < ymin) ymin = y;
			if (y > ymax) ymax = y;
		}
		if (ymin < 0)
			ymin = 0;
		if (ymax >= vres)
			ymax = vres - 1;

		for (y = ymin; y <= ymax; y++) {
			nx = 0;
			for (i = 0; i < count; i++) {
				j = (i + 1 == count) ? 0 : i + 1;
				ax = mmg_poly_xi(xf, xi, i);
				ay = mmg_poly_yi(yf, yi, i);
				bx = mmg_poly_xi(xf, xi, j);
				by = mmg_poly_yi(yf, yi, j);
				if (ay == by)
					continue;	/* horizontal: no crossing */
				/* half-open in y, so a shared vertex counts once */
				if ((y >= ay && y < by) || (y >= by && y < ay)) {
					if (nx < MMG_POLY_XS)
						mmg_pxs[nx++] = (short)
						    (ax + (long)(y - ay) *
						     (bx - ax) / (by - ay));
				}
			}
			/* insertion sort: nx is small and usually 2 */
			for (i = 1; i < nx; i++) {
				t = mmg_pxs[i];
				for (k = i; k > 0 && mmg_pxs[k - 1] > t; k--)
					mmg_pxs[k] = mmg_pxs[k - 1];
				mmg_pxs[k] = (short)t;
			}
			for (i = 0; i + 1 < nx; i += 2)
				if (mmg_pxs[i + 1] >= mmg_pxs[i])
					mmg_rc(rcs, &nr, fill, mmg_pxs[i], y,
					       mmg_pxs[i + 1], y);
		}
		mm_fill(rcs, nr, fill);
	}

outline:
	/* The border last so it survives the fill, which is the order the
	   triangle uses and the order MMBasic's fill leaves behind. */
	for (i = 0; i < count; i++) {
		j = (i + 1 == count) ? 0 : i + 1;
		mm_line(mmg_poly_xi(xf, xi, i), mmg_poly_yi(yf, yi, i),
			mmg_poly_xi(xf, xi, j), mmg_poly_yi(yf, yi, j), c);
	}
}

#endif /* MMB_GFX_POLYGON_H */

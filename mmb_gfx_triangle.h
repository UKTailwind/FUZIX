#ifndef MMB_GFX_TRIANGLE_H
#define MMB_GFX_TRIANGLE_H
/*
 *	TRIANGLE x1, y1, x2, y2, x3, y3 [, colour [, fill]]
 *
 *	See mmb_gfx_pts.h for why the primitives live in headers, one per
 *	primitive.
 *
 *	MMBasic's DrawTriangle (Draw.c): collinear points degenerate to a
 *	single line, an outline is three lines, and a fill is scanline
 *	spans between per-row extents walked along the three edges by a
 *	recording Bresenham (CalcLineInternal).  The lines go through the
 *	runtime's mm_line, which is the same Bresenham the LINE statement
 *	uses, so the outline lands on the same pixels.
 *
 *	The extent tables are static for the same reason as the circle's:
 *	two of them is nearly 2K and the program's stack is 8K.  They are
 *	sized for the tallest mode plus the firmware's own off-by-one -
 *	CalcLineInternal's vertical case clamps its end row to VRes
 *	INCLUSIVE, so row VRes is written even though it is never drawn,
 *	and a table sized exactly VRes would be overrun.
 */

#include "mmb_gfx_pts.h"

#define MMG_TMAX	482

static short mmg_txn[MMG_TMAX];
static short mmg_txx[MMG_TMAX];

/* Record the min/max x of one edge into the per-row tables.  MMBasic's
   CalcLineInternal, unchanged, with VRes taken from the kernel. */
static void mmg_tedge(int x1, int y1, int x2, int y2, int yoffset, int vres)
{
	int t, absX, absY, offX, offY, x, y, err, idx;

	if (y1 == y2) {
		if (y1 < 0 || y1 >= vres)
			return;
		idx = y1 - yoffset;
		if (x1 > x2) { t = x1; x1 = x2; x2 = t; }
		if (x1 < mmg_txn[idx])
			mmg_txn[idx] = (short)x1;
		if (x2 > mmg_txx[idx])
			mmg_txx[idx] = (short)x2;
		return;
	}

	if (x1 == x2) {
		if (y2 < y1) { t = y1; y1 = y2; y2 = t; }
		if (y1 < 0)
			y1 = 0;
		if (y2 > vres)
			y2 = vres;	/* the firmware's inclusive clamp */
		for (y = y1; y <= y2; y++) {
			idx = y - yoffset;
			if (x1 < mmg_txn[idx])
				mmg_txn[idx] = (short)x1;
			if (x1 > mmg_txx[idx])
				mmg_txx[idx] = (short)x1;
		}
		return;
	}

	if (y1 > y2) {
		t = y1; y1 = y2; y2 = t;
		t = x1; x1 = x2; x2 = t;
	}

	absX = (x1 < x2) ? x2 - x1 : x1 - x2;
	absY = y2 - y1;
	offX = x2 < x1 ? 1 : -1;
	offY = -1;

	x = x2;
	y = y2;
	if (y >= 0 && y < vres) {
		idx = y - yoffset;
		if (x < mmg_txn[idx])
			mmg_txn[idx] = (short)x;
		if (x > mmg_txx[idx])
			mmg_txx[idx] = (short)x;
	}

	if (absX > absY) {
		err = absX >> 1;
		while (x != x1) {
			err = err - absY;
			if (err < 0) {
				y += offY;
				err += absX;
			}
			x += offX;
			if (y >= 0 && y < vres) {
				idx = y - yoffset;
				if (x < mmg_txn[idx])
					mmg_txn[idx] = (short)x;
				if (x > mmg_txx[idx])
					mmg_txx[idx] = (short)x;
			}
		}
	} else {
		err = absY >> 1;
		while (y != y1) {
			err = err - absX;
			if (err < 0) {
				x += offX;
				err += absY;
			}
			y += offY;
			if (y >= 0 && y < vres) {
				idx = y - yoffset;
				if (x < mmg_txn[idx])
					mmg_txn[idx] = (short)x;
				if (x > mmg_txx[idx])
					mmg_txx[idx] = (short)x;
			}
		}
	}
}

static void mmg_triangle(int x0, int y0, int x1, int y1, int x2, int y2,
			 MMINTEGER c, MMINTEGER fill)
{
	short rcs[MMG_BATCH * 4];
	int nr = 0, t, vres, ymin, ymax, range, y, idx;

	/* collinear points have zero area: sort by y, draw one line */
	if (x0 * (y1 - y2) + x1 * (y2 - y0) + x2 * (y0 - y1) == 0) {
		if (y0 > y2) {
			t = y0; y0 = y2; y2 = t;
			t = x0; x0 = x2; x2 = t;
		}
		if (y0 > y1) {
			t = y0; y0 = y1; y1 = t;
			t = x0; x0 = x1; x1 = t;
		}
		if (y1 > y2) {
			t = y1; y1 = y2; y2 = t;
			t = x1; x1 = x2; x2 = t;
		}
		mm_line(x0, y0, x2, y2, c);
		return;
	}

	if (fill == MM_CUR) {
		mm_line(x0, y0, x1, y1, c);
		mm_line(x1, y1, x2, y2, c);
		mm_line(x2, y2, x0, y0, c);
		return;
	}

	/* sort the vertices by y */
	if (y0 > y2) {
		t = y0; y0 = y2; y2 = t;
		t = x0; x0 = x2; x2 = t;
	}
	if (y0 > y1) {
		t = y0; y0 = y1; y1 = t;
		t = x0; x0 = x1; x1 = t;
	}
	if (y1 > y2) {
		t = y1; y1 = y2; y2 = t;
		t = x1; x1 = x2; x2 = t;
	}

	vres = (int)mm_vres();
	if (vres <= 0)
		return;			/* no display: the host build */
	if (vres > MMG_TMAX - 2)
		vres = MMG_TMAX - 2;
	ymin = (y0 < 0) ? 0 : y0;
	ymax = (y2 >= vres) ? vres - 1 : y2;
	if (ymin >= vres || ymax < 0)
		return;
	range = ymax - ymin + 1;

	for (y = 0; y <= range; y++) {	/* one spare row for the clamp */
		mmg_txn[y] = 32767;
		mmg_txx[y] = -32768;
	}

	mmg_tedge(x0, y0, x1, y1, ymin, vres);
	mmg_tedge(x1, y1, x2, y2, ymin, vres);
	mmg_tedge(x2, y2, x0, y0, ymin, vres);

	for (y = ymin; y <= ymax; y++) {
		idx = y - ymin;
		if (mmg_txx[idx] >= mmg_txn[idx])
			mmg_rc(rcs, &nr, fill, mmg_txn[idx], y,
			       mmg_txx[idx], y);
	}
	mm_fill(rcs, nr, fill);

	mm_line(x0, y0, x1, y1, c);
	mm_line(x1, y1, x2, y2, c);
	mm_line(x2, y2, x0, y0, c);
}

#endif /* MMB_GFX_TRIANGLE_H */

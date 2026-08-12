#ifndef MMB_GFX_SPAN_H
#define MMB_GFX_SPAN_H
/*
 *	Per-row extent tables, and the recording Bresenham that fills them.
 *
 *	Shared by TRIANGLE and POLYGON, which fill the same way: walk every
 *	edge recording the leftmost and rightmost x it touches on each row,
 *	then emit one horizontal span per row.  This was TRIANGLE's and is
 *	moved here unchanged - POLYGON turned out to be the same algorithm
 *	with more edges, and the alternative was a second copy.
 *
 *	Nothing about the emitted C changes: a program still includes
 *	mmb_gfx_triangle.h and reaches these through it.
 *
 *	The tables are static rather than automatic for the reason the
 *	circle's are: two of them is nearly 2K against an 8K stack.  They
 *	are sized for the tallest mode plus the firmware's own off-by-one -
 *	CalcLineInternal's vertical case clamps its end row to VRes
 *	INCLUSIVE, so row VRes is written even though it is never drawn, and
 *	a table sized exactly VRes would be overrun.
 *
 *	Callers must clamp: yoffset >= 0, and the row range within MMG_TMAX.
 *	mmg_tedge guards y against vres but indexes the tables with
 *	y - yoffset unguarded, exactly as the firmware does.
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

#endif /* MMB_GFX_SPAN_H */

#ifndef MMB_GFX_RBOX_H
#define MMB_GFX_RBOX_H
/*
 *	RBOX x, y, width, height [, radius [, colour [, fill]]]
 *
 *	See mmb_gfx_pts.h for why the primitives live in headers, one per
 *	primitive.
 *
 *	MMBasic's cmd_rbox + DrawRBox (Draw.c), corner for corner.  The
 *	corner arcs are a Bresenham quarter-circle; a filled box draws
 *	them as spans and then paints the middle, an outline plots the
 *	arc pixels and joins them with four straight sides.  DrawRBox's
 *	corner spans arrive right-to-left, which DrawRectangle tolerates
 *	and the batched crossing does not - hence mmg_rectn.
 *
 *	The filled form ends by calling itself for the outline, exactly
 *	as the firmware does; the recursion is also why this header is
 *	always generated once included - cc1's name-count rule cannot
 *	drop a self-referential static, and the per-primitive include
 *	means it never has to.
 */

#include "mmb_gfx_pts.h"

static void mmg_rbox_c(int x1, int y1, int x2, int y2, int radius,
		       MMINTEGER c, MMINTEGER fill)
{
	short pts[MMG_BATCH * 2];
	short rcs[MMG_BATCH * 4];
	int np = 0, nr = 0;
	int f, ddF_x, ddF_y, xx, yy, maxr, t;

	if (x2 < x1) { t = x1; x1 = x2; x2 = t; }
	if (y2 < y1) { t = y1; y1 = y2; y2 = t; }
	if (radius < 0)
		radius = 0;
	/* the radius cannot exceed half of the shorter side, otherwise
	   the arcs do not meet the straight sides */
	maxr = (x2 - x1) / 2;
	if ((y2 - y1) / 2 < maxr)
		maxr = (y2 - y1) / 2;
	if (radius > maxr)
		radius = maxr;
	if (radius < 1) {
		/* degenerate to an ordinary rectangle */
		if (fill >= 0)
			mmg_rect1(fill, x1 + 1, y1 + 1, x2 - 1, y2 - 1);
		mmg_rect1(c, x1, y1, x2, y1);
		mmg_rect1(c, x1, y2, x2, y2);
		mmg_rect1(c, x1, y1, x1, y2);
		mmg_rect1(c, x2, y1, x2, y2);
		return;
	}

	f = 1 - radius;
	ddF_x = 1;
	ddF_y = -2 * radius;
	xx = 0;
	yy = radius;

	while (xx < yy) {
		if (f >= 0) {
			yy -= 1;
			ddF_y += 2;
			f += ddF_y;
		}
		xx += 1;
		ddF_x += 2;
		f += ddF_x;
		if (fill >= 0) {
			mmg_rectn(rcs, &nr, fill, x2 + xx - radius - 1,
				  y2 + yy - radius, x1 - xx + radius + 1,
				  y2 + yy - radius);
			mmg_rectn(rcs, &nr, fill, x2 + yy - radius - 1,
				  y2 + xx - radius, x1 - yy + radius + 1,
				  y2 + xx - radius);
			mmg_rectn(rcs, &nr, fill, x2 + xx - radius - 1,
				  y1 - yy + radius, x1 - xx + radius + 1,
				  y1 - yy + radius);
			mmg_rectn(rcs, &nr, fill, x2 + yy - radius - 1,
				  y1 - xx + radius, x1 - yy + radius + 1,
				  y1 - xx + radius);
		} else {
			mmg_pt(pts, &np, c, x2 + xx - radius, y2 + yy - radius);
			mmg_pt(pts, &np, c, x2 + yy - radius, y2 + xx - radius);
			mmg_pt(pts, &np, c, x1 - xx + radius, y2 + yy - radius);
			mmg_pt(pts, &np, c, x1 - yy + radius, y2 + xx - radius);
			mmg_pt(pts, &np, c, x2 + xx - radius, y1 - yy + radius);
			mmg_pt(pts, &np, c, x2 + yy - radius, y1 - xx + radius);
			mmg_pt(pts, &np, c, x1 - xx + radius, y1 - yy + radius);
			mmg_pt(pts, &np, c, x1 - yy + radius, y1 - xx + radius);
		}
	}
	if (fill >= 0) {
		mmg_rectn(rcs, &nr, fill, x1 + 1, y1 + radius,
			  x2 - 1, y2 - radius);
		mm_fill(rcs, nr, fill);
		mmg_rbox_c(x1, y1, x2, y2, radius, c, MM_CUR);
	} else {
		mmg_rectn(rcs, &nr, c, x1 + radius - 1, y1,
			  x2 - radius + 1, y1);
		mmg_rectn(rcs, &nr, c, x1 + radius - 1, y2,
			  x2 - radius + 1, y2);
		mmg_rectn(rcs, &nr, c, x1, y1 + radius, x1, y2 - radius);
		mmg_rectn(rcs, &nr, c, x2, y1 + radius, x2, y2 - radius);
		mm_fill(rcs, nr, c);
		mm_plot(pts, np, c);
	}
}

static void mmg_rbox(int x, int y, int wdt, int hgt, int radius,
		     MMINTEGER c, MMINTEGER fill)
{
	int x2, y2;

	/* cmd_rbox takes the radius with getint(0, 100) - a hard error
	   outside the range, not a clamp.  Width and height may be
	   negative, exactly as BOX. */
	if (radius < 0 || radius > 100)
		mm_error("Invalid radius");
	x2 = x + wdt + (wdt > 0 ? -1 : 1);
	y2 = y + hgt + (hgt > 0 ? -1 : 1);
	mmg_rbox_c(x, y, x2, y2, radius, c, fill);
}

#endif /* MMB_GFX_RBOX_H */

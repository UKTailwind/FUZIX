#ifndef MMB_GFX_ARC_H
#define MMB_GFX_ARC_H
/*
 *	ARC x, y, r1 [, r2], rad1, rad2 [, colour]
 *
 *	See mmb_gfx_pts.h for why the primitives live in headers, one per
 *	primitive.
 *
 *	MMBasic's cmd_arc (Draw.c): a ring segment between two radials,
 *	drawn as scanline spans over the bounding box, each pixel tested
 *	for membership of the sector.  Angles are MMBasic's compass ones -
 *	0 is up, 90 right - whole degrees, clockwise.  An omitted r2
 *	means a one-pixel-wide arc at r1, which the firmware expresses as
 *	r2 = r1, r1 - 1; the translator passes MM_CUR for the omission.
 *
 *	The firmware uses sqrtf/atan2f; this uses the double forms the
 *	runtime routes to the shared kernel libm.  Over radii bounded by
 *	the screen the truncated results agree.
 */

#include "mmb_gfx_pts.h"
#include <math.h>

static int mmg_arc_norm(int angle)
{
	angle %= 360;
	return angle < 0 ? angle + 360 : angle;
}

static int mmg_arc_in(int px, int py, int cx, int cy, int start_deg,
		      int end_deg)
{
	int dx = px - cx;
	int dy = py - cy;
	MMFLOAT angle;
	int angle_deg;

	if (dx == 0 && dy == 0)
		return 1;

	/* compass angles: 0 up, clockwise - atan2(dx, -dy) */
	angle = atan2((MMFLOAT)dx, (MMFLOAT)-dy) * 57.29577951;
	if (angle < 0)
		angle += 360.0;
	angle_deg = (int)angle;

	if (end_deg < start_deg)
		end_deg += 360;
	if (angle_deg < start_deg)
		angle_deg += 360;
	return (angle_deg >= start_deg && angle_deg <= end_deg);
}

static void mmg_arc(int x, int y, int r1, int r2, int rad1, int rad2,
		    MMINTEGER c)
{
	short rcs[MMG_BATCH * 4];
	int nr = 0;
	int scan_y, dy, dy2, dx_outer, dx_inner, side;
	int x_start, x_end, scan_x, seg_start, seg_end;

	if (r2 == (int)MM_CUR) {
		r2 = r1;
		r1--;
	}
	if (r2 < r1)
		MM_RAISE("Inner radius < outer");

	rad1 = mmg_arc_norm(rad1);
	rad2 = mmg_arc_norm(rad2);
	if (rad1 == rad2)
		MM_RAISE("Radials");
	if (rad2 < rad1)
		rad2 += 360;

	for (scan_y = y - r2; scan_y <= y + r2; scan_y++) {
		dy = scan_y - y;
		dy2 = dy * dy;
		dx_outer = (int)sqrt((MMFLOAT)(r2 * r2 - dy2));
		dx_inner = (r1 * r1 > dy2)
			   ? (int)sqrt((MMFLOAT)(r1 * r1 - dy2)) : 0;

		for (side = 0; side < 2; side++) {
			if (side == 0) {
				x_start = x - dx_outer;
				x_end = x - dx_inner;
			} else {
				x_start = x + dx_inner;
				x_end = x + dx_outer;
			}

			seg_start = -1;
			seg_end = -1;
			for (scan_x = x_start; scan_x <= x_end; scan_x++) {
				if (mmg_arc_in(scan_x, scan_y, x, y,
					       rad1, rad2)) {
					if (seg_start == -1)
						seg_start = scan_x;
					seg_end = scan_x;
				} else if (seg_start != -1) {
					mmg_rc(rcs, &nr, c, seg_start,
					       scan_y, seg_end, scan_y);
					seg_start = -1;
				}
			}
			if (seg_start != -1)
				mmg_rc(rcs, &nr, c, seg_start, scan_y,
				       seg_end, scan_y);
		}
	}
	mm_fill(rcs, nr, c);
}

#endif /* MMB_GFX_ARC_H */

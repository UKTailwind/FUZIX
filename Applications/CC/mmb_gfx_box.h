#ifndef MMB_GFX_BOX_H
#define MMB_GFX_BOX_H
/*
 *	BOX x, y, width, height [, lw [, colour [, fill]]]
 *
 *	See mmb_gfx_pts.h for why the primitives live in headers, one per
 *	primitive.
 *
 *	MMBasic's cmd_box + DrawBox (Draw.c).  A box is at most five
 *	rectangles - four edges and a fill - so each goes across as its
 *	own crossing (mmg_rect1) rather than through a batch.
 *
 *	The width/height arguments may be negative and the box is drawn
 *	the other way, exactly as the firmware does it: the far corner is
 *	x + width - 1 for positive width and x + width + 1 for negative,
 *	and DrawBox then normalises.  A zero width or height draws
 *	nothing, which is cmd_box's own guard.
 */

#include "mmb_gfx_pts.h"

static void mmg_box(int x, int y, int wdt, int hgt, int lw,
		    MMINTEGER c, MMINTEGER fill)
{
	int x1 = x, y1 = y, x2, y2, w = lw, t;

	/* cmd_box takes the line width with getint(0, 100), which is a
	   hard error outside the range, not a clamp. */
	if (lw < 0 || lw > 100)
		MM_RAISE("Invalid line width");
	if (wdt == 0 || hgt == 0)
		return;
	x2 = x + wdt + (wdt > 0 ? -1 : 1);
	y2 = y + hgt + (hgt > 0 ? -1 : 1);

	/* DrawBox from here on. */
	if (x2 <= x1) { t = x1; x1 = x2; x2 = t; }
	if (y2 <= y1) { t = y1; y1 = y2; y2 = t; }
	if (w > x2 - x1)
		w = x2 - x1;
	if (w > y2 - y1)
		w = y2 - y1;

	if (w > 0) {
		w--;
		mmg_rect1(c, x1, y1, x2, y1 + w);	/* top    */
		mmg_rect1(c, x1, y2 - w, x2, y2);	/* bottom */
		mmg_rect1(c, x1, y1, x1 + w, y2);	/* left   */
		mmg_rect1(c, x2 - w, y1, x2, y2);	/* right  */
		w++;
	}
	if (fill >= 0)
		mmg_rect1(fill, x1 + w, y1 + w, x2 - w, y2 - w);
}

#endif /* MMB_GFX_BOX_H */

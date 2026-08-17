#ifndef MMB_GFX_LINE_H
#define MMB_GFX_LINE_H
/*
 *	LINE x1, y1, x2, y2, width [, colour]  -  the WIDE forms.
 *
 *	See mmb_gfx_pts.h for why the primitives live in headers, one per
 *	primitive.  A one-pixel line stays in the runtime (mm_line): it is
 *	in bcrun already and every program has it, so only the width costs
 *	anything and only the programs that ask for one pay.
 *
 *	MMBasic's DrawLine (Draw.c:1090) is FOUR different algorithms
 *	chosen by shape, and they are not approximations of each other:
 *
 *	  horizontal (y1 == y2), w > 0   one rectangle, y2 + w - 1
 *	  vertical   (x1 == x2), w > 0   one rectangle, x2 + w - 1
 *	  |w| == 1                       Bresenham, one pixel
 *	  anything else                  a STAMP: step along the line one
 *	                                 pixel at a time and, at each
 *	                                 step, plot the points from
 *	                                 `start` to `end` along the
 *	                                 PERPENDICULAR in 0.25 increments
 *
 *	Note where the width goes.  A positive w hangs the whole width off
 *	ONE side (start = 0, end = w); a NEGATIVE w centres it on the line
 *	(start = -w/2, end = w/2).  That asymmetry is deliberate in the
 *	firmware and programs rely on it - a horizontal w=3 line is drawn
 *	at y, y+1, y+2, not y-1, y, y+1.
 *
 *	The 0.25 step means the stamp plots the same pixel several times.
 *	Harmless - same colour - and copied rather than optimised away,
 *	because the rounding of the ones it does NOT repeat is what
 *	decides the edge of a thick diagonal.
 */

#include "mmb_gfx_pts.h"
#include <math.h>

static void mmg_linew(int x1, int y1, int x2, int y2, int w, MMINTEGER c)
{
	short buf[MMG_BATCH * 2];
	int nb = 0;
	MMFLOAT dx, dy, length, nx, ny, px, py, start, end, j;
	int i, steps;

	/* The two axis-aligned cases, which are what a maze or a bar
	   chart is made of: one rectangle each, exactly as the firmware
	   does it - and note it is w - 1, so w = 1 is one pixel. */
	if (y1 == y2 && w > 0) {
		mmg_rect1(c, x1, y1, x2, y2 + w - 1);
		return;
	}
	if (x1 == x2 && w > 0) {
		mmg_rect1(c, x1, y1, x2 + w - 1, y2);
		return;
	}
	if (w == 1 || w == -1) {
		mm_line(x1, y1, x2, y2, c);
		return;
	}
	if (w < 0) {
		w = -w;
		start = -((MMFLOAT)w / 2.0);
		end = (MMFLOAT)w / 2.0;
	} else {
		start = 0.0;
		end = (MMFLOAT)w;
	}
	dx = (MMFLOAT)(x2 - x1);
	dy = (MMFLOAT)(y2 - y1);
	length = sqrt(dx * dx + dy * dy);
	if (length == 0.0) {
		/* Both ends the same point AND a negative width - the only
		   way to reach here, since a positive width takes the
		   axis-aligned branch above.  The firmware divides by zero,
		   gets NaN for the direction and converts NaN to int, which
		   is undefined: there is no behaviour here to match.  Draw
		   nothing, rather than invent a pixel the interpreter does
		   not reliably put anywhere. */
		return;
	}
	nx = dx / length;
	ny = dy / length;
	px = -ny;
	py = nx;
	steps = (int)length;
	for (i = 0; i <= steps; i++) {
		MMFLOAT lx = (MMFLOAT)x1 + (MMFLOAT)i * nx;
		MMFLOAT ly = (MMFLOAT)y1 + (MMFLOAT)i * ny;
		for (j = start; j <= end; j += 0.25) {
			mmg_pt(buf, &nb, c,
			       (int)floor(lx + j * px + 0.5),
			       (int)floor(ly + j * py + 0.5));
		}
	}
	if (nb)
		mm_plot(buf, nb, c);
}

#endif /* MMB_GFX_LINE_H */

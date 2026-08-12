#ifndef MMB_GFX_BEZIER_H
#define MMB_GFX_BEZIER_H
/*
 *	BEZIER xarray(), yarray() [, n] [, colour]
 *
 *	MMBasic's cmd_bezier -> PlotBezier (graphics/Draw.c), transcribed.
 *	An n-point Bezier: n control points, degree n-1, evaluated with the
 *	Bernstein basis in 24-bit fixed point.
 *
 *	INTEGER arrays only, which is MMBasic's own restriction -
 *	cmd_bezier reads them with parseintegerarray, unlike POLYGON which
 *	takes either.  The translator says so rather than converting
 *	silently, because a float array here is a program that would work
 *	on a PicoMite only by accident.
 *
 *	n defaults to the array length and may be given to use fewer
 *	points; MMBasic bounds it at 2.
 *
 *	THE STEP COUNT IS NOT A ROUND NUMBER and is worth keeping exactly:
 *	three times the diagonal of the control points' bounding box,
 *	clamped to [10, 2000].  Fewer steps and a long curve breaks into
 *	visible chords; more and it redraws the same pixels.  Copying the
 *	rule rather than inventing one means a curve lands where a
 *	PicoMite would put it.
 *
 *	The run-length coalescing below - collecting consecutive steps
 *	that move in the same direction and drawing them as one line -
 *	is also MMBasic's.  It does not change which pixels are lit, since
 *	the steps are unit moves in a constant direction and Bresenham
 *	over the same endpoints lights the same ones; it changes how many
 *	calls the drawing takes, which matters more here than there
 *	because each one crosses into the kernel.
 *
 *	ONE DELIBERATE DIFFERENCE: sixteen control points maximum, with an
 *	error past it.  MMBasic sizes binom_coeffs[], t_powers[] and
 *	omt_powers[] at 16 and bounds n only by the array length, so a
 *	seventeen-element array walks off three stack arrays there.  This
 *	refuses instead.
 */

#include "mmb_gfx_pts.h"

#define MMG_BEZ_MAX	16		/* MMBasic's array sizes */
#define MMG_BEZ_SHIFT	24
#define MMG_BEZ_ONE	(1L << MMG_BEZ_SHIFT)
#define MMG_BEZ_HALF	(1L << (MMG_BEZ_SHIFT - 1))
#define MMG_BEZ_MUL(a, b) \
	(long)(((long long)(a) * (long long)(b)) >> MMG_BEZ_SHIFT)

/* MMBasic's binomial(): the multiplicative form, which stays exact for
   the small n this allows because the running product is divisible at
   every step. */
static int mmg_bez_binom(int n, int k)
{
	int result = 1, i;

	if (k > n)
		return 0;
	if (k == 0 || k == n)
		return 1;
	for (i = 0; i < k; i++) {
		result *= (n - i);
		result /= (i + 1);
	}
	return result;
}

/* MMBasic's isqrt(): Newton, integer. */
static int mmg_bez_isqrt(int n)
{
	int x, y;

	if (n < 2)
		return n;
	x = n;
	y = (x + 1) / 2;
	while (y < x) {
		x = y;
		y = (x + n / x) / 2;
	}
	return x;
}

static void mmg_bezier(const MMINTEGER *x, const MMINTEGER *y,
		       MMINTEGER n, MMINTEGER avail, MMINTEGER c)
{
	int binom[MMG_BEZ_MAX];
	long tp[MMG_BEZ_MAX], omt[MMG_BEZ_MAX];
	int min_x, max_x, min_y, max_y, i, step, steps;
	int prev_x, prev_y, curr_x, curr_y, dx, dy;
	int lsx, lsy, ldx = 0, ldy = 0, llen = 0;
	long t_fp, omt_fp, basis;
	long long xv, yv;
	int np;

	if (n == 0)
		n = avail;
	if (n < 2) {
		mm_error("Invalid number of points");
		return;
	}
	if (n > avail) {
		mm_error("Dimensions");
		return;
	}
	if (n > MMG_BEZ_MAX) {
		mm_error("BEZIER takes at most 16 control points");
		return;
	}
	np = (int)n;

	min_x = max_x = (int)x[0];
	min_y = max_y = (int)y[0];
	for (i = 1; i < np; i++) {
		if ((int)x[i] < min_x) min_x = (int)x[i];
		if ((int)x[i] > max_x) max_x = (int)x[i];
		if ((int)y[i] < min_y) min_y = (int)y[i];
		if ((int)y[i] > max_y) max_y = (int)y[i];
	}
	steps = mmg_bez_isqrt((max_x - min_x) * (max_x - min_x) +
			      (max_y - min_y) * (max_y - min_y)) * 3;
	if (steps < 10)
		steps = 10;
	if (steps > 2000)
		steps = 2000;

	for (i = 0; i < np; i++)
		binom[i] = mmg_bez_binom(np - 1, i);

	prev_x = (int)x[0];
	prev_y = (int)y[0];
	mm_pixel(prev_x, prev_y, c);
	lsx = prev_x;
	lsy = prev_y;

	for (step = 1; step <= steps; step++) {
		t_fp = (long)(((long long)step << MMG_BEZ_SHIFT) / steps);
		omt_fp = MMG_BEZ_ONE - t_fp;
		tp[0] = MMG_BEZ_ONE;
		omt[0] = MMG_BEZ_ONE;
		for (i = 1; i < np; i++) {
			tp[i] = MMG_BEZ_MUL(tp[i - 1], t_fp);
			omt[i] = MMG_BEZ_MUL(omt[i - 1], omt_fp);
		}
		xv = 0;
		yv = 0;
		for (i = 0; i < np; i++) {
			basis = MMG_BEZ_MUL(omt[np - 1 - i], tp[i]);
			basis = basis * binom[i];
			xv += (long long)basis * x[i];
			yv += (long long)basis * y[i];
		}
		curr_x = (int)((xv + MMG_BEZ_HALF) >> MMG_BEZ_SHIFT);
		curr_y = (int)((yv + MMG_BEZ_HALF) >> MMG_BEZ_SHIFT);

		if (curr_x == prev_x && curr_y == prev_y)
			continue;

		dx = curr_x - prev_x;
		dy = curr_y - prev_y;
		if (dx > 1 || dx < -1 || dy > 1 || dy < -1) {
			/* a jump: flush the run and bridge it */
			if (llen > 1)
				mm_line(lsx, lsy, prev_x, prev_y, c);
			else if (llen == 1)
				mm_pixel(prev_x, prev_y, c);
			mm_line(prev_x, prev_y, curr_x, curr_y, c);
			llen = 0;
		} else if (llen > 0 && dx == ldx && dy == ldy) {
			llen++;
		} else {
			if (llen > 1)
				mm_line(lsx, lsy, prev_x, prev_y, c);
			else if (llen == 1)
				mm_pixel(prev_x, prev_y, c);
			lsx = prev_x;
			lsy = prev_y;
			ldx = dx;
			ldy = dy;
			llen = 1;
		}
		prev_x = curr_x;
		prev_y = curr_y;
	}

	if (llen > 1)
		mm_line(lsx, lsy, prev_x, prev_y, c);
	else if (llen == 1)
		mm_pixel(prev_x, prev_y, c);
}

#endif /* MMB_GFX_BEZIER_H */

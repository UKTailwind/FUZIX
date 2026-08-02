#ifndef MMB_GFX_H
#define MMB_GFX_H
/*
 *	The drawing primitives that are pure geometry.
 *
 *	These are NOT in bcrun. bcrun is loaded for every translated
 *	program and shares one 256K process with the program's own code
 *	and its 48K of data space, so a byte added there is a byte taken
 *	from every program on the machine - including the ones that draw
 *	nothing. Here they cost only the programs that use them, and only
 *	while that program is the resident one.
 *
 *	Every function is static, and cc1 generates nothing for a file
 *	scope static that nothing else names (hosttest/deadstatic.sh), so
 *	a program that calls CIRCLE pays for the circle and not for the
 *	rest of this file. There is no linker on this target to do that
 *	afterwards, which is why the compiler learned to do it.
 *
 *	The only things reaching outside are mm_plot and mm_fill - a run
 *	of points and a run of rectangles - so however many primitives
 *	end up here, the kernel and bcrun stay the size they are.
 *
 *	The algorithms are MMBasic's own, from Draw.c, so that a
 *	translated program draws the same pixels as the interpreter. That
 *	matters for more than looks: comparing pixel counts against
 *	MMBasic is how a code generation bug was found here once already.
 */

#include "mmb_runtime.h"

/*
 *	Items buffered before crossing into the kernel. Points cost two
 *	shorts and rectangles four, so this is 128 and 256 bytes of the
 *	caller's stack; both live in the primitive's own frame rather
 *	than in static storage, so an unused header costs no BSS either.
 */
#define MMG_BATCH	32

static void mmg_pt(short *b, int *n, MMINTEGER c, int x, int y)
{
	b[*n * 2] = (short)x;
	b[*n * 2 + 1] = (short)y;
	if (++*n == MMG_BATCH) {
		mm_plot(b, *n, c);
		*n = 0;
	}
}

static void mmg_rc(short *b, int *n, MMINTEGER c, int x1, int y1,
		   int x2, int y2)
{
	b[*n * 4] = (short)x1;
	b[*n * 4 + 1] = (short)y1;
	b[*n * 4 + 2] = (short)x2;
	b[*n * 4 + 3] = (short)y2;
	if (++*n == MMG_BATCH) {
		mm_fill(b, *n, c);
		*n = 0;
	}
}

/*
 *	CIRCLE x, y, r [, lw [, aspect [, colour [, fill]]]]
 *
 *	MMBasic's DrawCircle (Draw.c), which is Bresenham with the x axis
 *	scaled by a 10 bit fixed point aspect ratio. Two passes, as there:
 *	the fill is drawn first as horizontal spans - four per step, above
 *	and below the centre on both octant pairs - and the outline over
 *	it as eight points per step.
 *
 *	fill is MM_CUR for "no fill", matching the interpreter's -1.
 *
 *	Difference from the interpreter, deliberate: a wide border is
 *	drawn as lw concentric outlines, which is MMBasic's own loop run
 *	more than once. The firmware routes lw > 1 to a separate
 *	line-by-line ring routine that corrects the aspect of the inner
 *	edge; that is a speed and quality optimisation for the hardware
 *	and is not ported. lw 1 - overwhelmingly the common case - is
 *	pixel for pixel the same.
 */
static void mmg_circle(int x, int y, int radius, int w, MMINTEGER c,
		       MMINTEGER fill, MMFLOAT aspect)
{
	short pts[MMG_BATCH * 2];
	short rcs[MMG_BATCH * 4];
	int np = 0, nr = 0;
	int a, b, P, A, B;
	int asp = (int)(aspect * 1024.0);
	int w1 = w, r1 = radius;

	if (radius <= 0)
		return;

	if (fill != MM_CUR) {
		while (w >= 0 && radius > 0) {
			a = 0;
			b = radius;
			P = 1 - radius;
			do {
				A = (a * asp) >> 10;
				B = (b * asp) >> 10;
				mmg_rc(rcs, &nr, fill, x - A, y + b, x + A, y + b);
				mmg_rc(rcs, &nr, fill, x - A, y - b, x + A, y - b);
				mmg_rc(rcs, &nr, fill, x - B, y + a, x + B, y + a);
				mmg_rc(rcs, &nr, fill, x - B, y - a, x + B, y - a);
				if (P < 0) {
					P += 3 + (a << 1);
					a++;
				} else {
					P += 5 + ((a - b) << 1);
					a++;
					b--;
				}
			} while (a <= b);
			w--;
			radius--;
		}
		mm_fill(rcs, nr, fill);
		nr = 0;
	}

	/*
	 * The interpreter skips the outline when it is the fill colour,
	 * the fill having already drawn it. Its test is a bare c == fill
	 * because -1 there means "no fill" and can never equal a colour.
	 * Ours can: with no colour and no fill given, both are MM_CUR,
	 * and a plain CIRCLE would draw nothing at all.
	 */
	if (fill != MM_CUR && c == fill)
		return;

	w = w1;
	radius = r1;
	while (w >= 0 && radius > 0) {
		a = 0;
		b = radius;
		P = 1 - radius;
		do {
			A = (a * asp) >> 10;
			B = (b * asp) >> 10;
			/*
			 * The interpreter's own guard, and it is not
			 * decoration: the loop always runs one pass more
			 * than the border is thick, and that last pass
			 * must step the Bresenham state without drawing.
			 * Without it lw 1 draws radius r AND r-1, and a
			 * circle comes out two pixels thick everywhere.
			 */
			if (w) {
				mmg_pt(pts, &np, c, A + x, b + y);
				mmg_pt(pts, &np, c, B + x, a + y);
				mmg_pt(pts, &np, c, x - A, b + y);
				mmg_pt(pts, &np, c, x - B, a + y);
				mmg_pt(pts, &np, c, B + x, y - a);
				mmg_pt(pts, &np, c, A + x, y - b);
				mmg_pt(pts, &np, c, x - A, y - b);
				mmg_pt(pts, &np, c, x - B, y - a);
			}
			if (P < 0) {
				P += 3 + (a << 1);
				a++;
			} else {
				P += 5 + ((a - b) << 1);
				a++;
				b--;
			}
		} while (a <= b);
		w--;
		radius--;
	}
	mm_plot(pts, np, c);
}

#endif /* MMB_GFX_H */

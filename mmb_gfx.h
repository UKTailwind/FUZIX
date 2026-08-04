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

/*
 *	Largest radius the span based paths handle. Beyond it they fall
 *	back to stepping outlines, which is worse looking but bounded:
 *	the extent tables below are the only large storage here, and a
 *	radius past the long side of the screen is off it anyway.
 */
#define MMG_RMAX	480

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
 *	Half width of a Bresenham circle for every row of it, x scaled by
 *	a 10 bit fixed point aspect. e[k] is the widest x reached on the
 *	row k above or below the centre.
 *
 *	Each Bresenham step names two rows - b with half width A, and a
 *	with half width B - and between them the steps name every row
 *	from 0 to r, which is why one pass fills the table.
 */
static void mmg_extent(short *e, int r, int asp)
{
	int a = 0, b = r, P = 1 - r, A, B, i;

	for (i = 0; i <= r; i++)
		e[i] = 0;
	do {
		A = (a * asp) >> 10;
		B = (b * asp) >> 10;
		if (A > e[b])
			e[b] = (short)A;
		if (B > e[a])
			e[a] = (short)B;
		if (P < 0) {
			P += 3 + (a << 1);
			a++;
		} else {
			P += 5 + ((a - b) << 1);
			a++;
			b--;
		}
	} while (a <= b);
	/* A circle only gets wider towards the centre; enforcing that
	   means a span can never be taken from an entry the loop missed. */
	for (i = r - 1; i >= 0; i--)
		if (e[i] < e[i + 1])
			e[i] = e[i + 1];
}

/*
 *	Scratch for the above. Static rather than automatic because two
 *	of them is nearly 2K and the program's stack is 8K; it is BSS
 *	only in a program that draws a circle, since nothing includes
 *	this header otherwise.
 */
static short mmg_eo[MMG_RMAX + 1];
static short mmg_ei[MMG_RMAX + 1];

/*
 *	A ring: everything inside radius r2 and outside radius r1, drawn
 *	as one span per row - two where the row crosses the hole.
 *
 *	This is what MMBasic does for a thick border, and the reason it
 *	does it is worth recording: concentric outlines DO NOT TILE. Step
 *	a Bresenham circle at r and again at r-1 and the two tracks part
 *	company near the diagonals, leaving holes. That is why the
 *	firmware carries a routine that looks far more expensive than
 *	stepping the outline w times - it is the only one that comes out
 *	solid. Spans do not care what the arithmetic did: the row is
 *	filled from edge to edge.
 *
 *	The firmware builds a bitmap of the outer disc and subtracts the
 *	inner one, a scanline at a time, walking the whole circle for
 *	every row. This takes the two edges from a table built in one
 *	pass instead, which is the same pixels for a fraction of the
 *	work.
 */
static void mmg_ring(int x, int y, int r1, int r2, MMINTEGER c,
		     MMFLOAT aspect, MMFLOAT aspect2)
{
	short rcs[MMG_BATCH * 4];
	int nr = 0, dy, ay, wo, wi;

	if (r2 <= 0 || r2 > MMG_RMAX)
		return;
	if (r1 < 0)
		r1 = 0;

	mmg_extent(mmg_eo, r2, (int)(aspect * 1024.0));
	if (r1 > 0)
		mmg_extent(mmg_ei, r1, (int)(aspect2 * 1024.0));

	for (dy = -r2; dy <= r2; dy++) {
		ay = (dy < 0) ? -dy : dy;
		wo = mmg_eo[ay];
		if (r1 > 0 && ay <= r1) {
			wi = mmg_ei[ay];
			if (wi >= wo)
				continue;	/* the hole swallows the row */
			mmg_rc(rcs, &nr, c, x - wo, y + dy, x - wi - 1, y + dy);
			mmg_rc(rcs, &nr, c, x + wi + 1, y + dy, x + wo, y + dy);
		} else {
			mmg_rc(rcs, &nr, c, x - wo, y + dy, x + wo, y + dy);
		}
	}
	mm_fill(rcs, nr, c);
}

/*
 *	CIRCLE x, y, r [, lw [, aspect [, colour [, fill]]]]
 *
 *	MMBasic's DrawCircle (Draw.c), including its dispatch: a thick
 *	border with a fill is two filled circles, a thick border without
 *	one is a ring, and a single width border is Bresenham plotting
 *	eight points a step with the fill drawn under it as spans.
 *
 *	fill is MM_CUR for "no fill", matching the interpreter's -1.
 *
 *	A stretched outline (aspect > 1) used to come out dotted, here and
 *	in the interpreter: A steps by the aspect, so consecutive points
 *	of the circle algorithm are more than one pixel apart and single
 *	pixels leave holes. The interpreter has since been fixed and this
 *	is that fix - join consecutive points with short horizontal runs,
 *	and bridge the gap the two octants leave at the 45 degree point.
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
	int stretched, lastA, lastB, lastb;
	MMFLOAT aspect2;

	if (radius <= 0 || w < 0)
		return;

	if (w > 1) {
		/* The interpreter divides by radius - w without checking;
		   a border thicker than the circle is a filled one here
		   rather than a division by zero. */
		aspect2 = ((aspect * (MMFLOAT)radius) - (MMFLOAT)w)
			  / (MMFLOAT)(radius - w > 0 ? radius - w : 1);
		if (fill != MM_CUR) {
			/* border and centre are different colours: two
			   filled circles, the smaller over the larger */
			mmg_circle(x, y, radius, 0, c, c, aspect);
			mmg_circle(x, y, radius - w, 0, fill, fill, aspect2);
		} else {
			mmg_ring(x, y, radius - w, radius, c, aspect, aspect2);
		}
		return;
	}

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
	/* Stretched: consecutive points are more than a pixel apart, so
	   they are joined with runs rather than plotted singly. */
	stretched = (asp > 1024);

	while (w >= 0 && radius > 0) {
		a = 0;
		b = radius;
		P = 1 - radius;
		lastA = 0;
		lastB = (b * asp) >> 10;
		lastb = b;
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
				if (stretched) {
					/* A only rises and B only falls, so
					   every run below is left to right */
					mmg_rc(rcs, &nr, c, x + lastA, y + b, x + A, y + b);
					mmg_rc(rcs, &nr, c, x - A, y + b, x - lastA, y + b);
					mmg_rc(rcs, &nr, c, x + lastA, y - b, x + A, y - b);
					mmg_rc(rcs, &nr, c, x - A, y - b, x - lastA, y - b);
					mmg_rc(rcs, &nr, c, x + B, y + a, x + lastB, y + a);
					mmg_rc(rcs, &nr, c, x - lastB, y + a, x - B, y + a);
					mmg_rc(rcs, &nr, c, x + B, y - a, x + lastB, y - a);
					mmg_rc(rcs, &nr, c, x - lastB, y - a, x - B, y - a);
				} else {
					mmg_pt(pts, &np, c, A + x, b + y);
					mmg_pt(pts, &np, c, B + x, a + y);
					mmg_pt(pts, &np, c, x - A, b + y);
					mmg_pt(pts, &np, c, x - B, a + y);
					mmg_pt(pts, &np, c, B + x, y - a);
					mmg_pt(pts, &np, c, A + x, y - b);
					mmg_pt(pts, &np, c, x - A, y - b);
					mmg_pt(pts, &np, c, x - B, y - a);
				}
			}
			lastA = A;
			lastB = B;
			lastb = b;
			if (P < 0) {
				P += 3 + (a << 1);
				a++;
			} else {
				P += 5 + ((a - b) << 1);
				a++;
				b--;
			}
		} while (a <= b);
		if (w && stretched) {
			/* The two octants stop short of each other at the
			   45 degree point, leaving a gap of aspect-1
			   pixels: bridge the last row of the flat octant. */
			mmg_rc(rcs, &nr, c, x + lastA, y + lastb, x + lastB, y + lastb);
			mmg_rc(rcs, &nr, c, x - lastB, y + lastb, x - lastA, y + lastb);
			mmg_rc(rcs, &nr, c, x + lastA, y - lastb, x + lastB, y - lastb);
			mmg_rc(rcs, &nr, c, x - lastB, y - lastb, x - lastA, y - lastb);
		}
		w--;
		radius--;
	}
	if (stretched)
		mm_fill(rcs, nr, c);
	else
		mm_plot(pts, np, c);
}

#endif /* MMB_GFX_H */

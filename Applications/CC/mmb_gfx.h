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

/*
 *	--- TEXT ---------------------------------------------------------
 *
 *	TEXT x, y, string$ [, alignment$] [, font] [, scale] [, fg] [, bg]
 *
 *	MMBasic's cmd_text and GUIPrintString (Draw.c).  The kernel draws
 *	a run of glyphs in a font, at a size, in two colours; deciding
 *	WHERE that run starts is all of the rest, and it is arithmetic on
 *	the font's cell, so it belongs here.
 *
 *	The cell comes from the kernel, per call, because the fonts are
 *	the kernel's - MMBasic's nine, in flash.  A copy of "8 by 12"
 *	here would centre font 3 as if it were font 1.
 */

/* MMBasic's own numbering (Draw.h), so the parse below can stay its. */
#define MMG_JUST_LEFT	0
#define MMG_JUST_CENTRE	1
#define MMG_JUST_RIGHT	2
#define MMG_JUST_TOP	0
#define MMG_JUST_MIDDLE	1
#define MMG_JUST_BOTTOM	2
#define MMG_ORIENT_N	0		/* normal                       */
#define MMG_ORIENT_V	1		/* one glyph per line, downwards */
#define MMG_ORIENT_I	2		/* inverted                     */
#define MMG_ORIENT_U	3		/* rotated CCW 90               */
#define MMG_ORIENT_D	4		/* rotated CW 90                */

/*
 *	The justify$ of TEXT and GUI CAPTION: up to three letters, in the
 *	order horizontal, vertical, orientation, any of them absent.
 *
 *	MMBasic's GetJustification, transliterated including its
 *	forgiveness - a letter that is not of the set this switch is
 *	looking for is pushed back and offered to the next one, which is
 *	what lets "B" mean bottom and "R" mean right with neither needing
 *	a place-holder.  Returns 0 if the string is not a justification at
 *	all, and MMBasic treats that as an error.
 */
static int mmg_upper(int c)
{
	return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
}

static int mmg_just(const char *p, int *jh, int *jv, int *jo)
{
	int c;

	c = mmg_upper(*p++);
	if (c == 'L')      *jh = MMG_JUST_LEFT;
	else if (c == 'C') *jh = MMG_JUST_CENTRE;
	else if (c == 'R') *jh = MMG_JUST_RIGHT;
	else if (c == 0)   return 1;
	else               p--;
	while (*p == ' ')
		p++;

	c = mmg_upper(*p++);
	if (c == 'T')      *jv = MMG_JUST_TOP;
	else if (c == 'M') *jv = MMG_JUST_MIDDLE;
	else if (c == 'B') *jv = MMG_JUST_BOTTOM;
	else if (c == 0)   return 1;
	else               p--;
	while (*p == ' ')
		p++;

	c = mmg_upper(*p++);
	if (c == 'N')      *jo = MMG_ORIENT_N;
	else if (c == 'V') *jo = MMG_ORIENT_V;
	else if (c == 'I') *jo = MMG_ORIENT_I;
	else if (c == 'U') *jo = MMG_ORIENT_U;
	else if (c == 'D') *jo = MMG_ORIENT_D;
	else if (c == 0)   return 1;
	else               return 0;
	return *p == 0;
}

/*
 *	TEXT itself.
 *
 *	The justification arithmetic is GUIPrintString's, unchanged, with
 *	its three cases: normal text moves by the whole string's width and
 *	one cell's height, vertical text by one cell's width and the whole
 *	string's height, and the rotated ones by the two swapped.
 *
 *	Only N and V are drawn.  I, U and D rotate the GLYPH, which needs
 *	the font's bits on this side of the syscall; until that is done
 *	they are drawn normally rather than not at all, so a program that
 *	asks for them shows something wrong rather than nothing.
 *
 *	s and just are MMBasic strings - length byte first - not C ones,
 *	which is why the length is taken and not counted: a string here
 *	may contain a NUL and still be worth drawing.
 */
static void mmg_text(int x, int y, const char *s, const char *just,
		     int font, int scale, MMINTEGER fc, MMINTEGER bc)
{
	int jh = MMG_JUST_LEFT, jv = MMG_JUST_TOP, jo = MMG_ORIENT_N;
	MMINTEGER cw = 0, ch = 0;
	const char *text = mm_cstr(s);
	int len = mm_slen(s);
	int w, h, i;

	if (font < 1)
		font = 1;
	if (scale < 1)
		scale = 1;
	if (scale > 15)
		scale = 15;
	if (just && mm_slen(just) &&
	    !mmg_just(mm_cstr(just), &jh, &jv, &jo))
		mm_error("Justification");
	/* Negative is "there is no display", which is the host build: draw
	   nothing and say nothing, as every other primitive does there.
	   Zero is a font the kernel really does not have, and MMBasic
	   makes that an error. */
	if (mm_fontinfo(font, &cw, &ch) < 0)
		return;
	if (!cw)
		mm_error("Invalid font");

	w = (int)cw * scale;		/* one cell, scaled - GetFontWidth */
	h = (int)ch * scale;

	if (jo == MMG_ORIENT_V) {
		if (jh == MMG_JUST_CENTRE)
			x -= w / 2;
		else if (jh == MMG_JUST_RIGHT)
			x -= w;
		if (jv == MMG_JUST_MIDDLE)
			y -= (len * h) / 2;
		else if (jv == MMG_JUST_BOTTOM)
			y -= len * h;
		/* One call per glyph: the run the kernel draws is a row,
		   and this is a column. */
		for (i = 0; i < len; i++)
			mm_gtext(x, y + i * h, font, scale, fc, bc,
				 text + i, 1);
		return;
	}

	if (jh == MMG_JUST_CENTRE)
		x -= (len * w) / 2;
	else if (jh == MMG_JUST_RIGHT)
		x -= len * w;
	if (jv == MMG_JUST_MIDDLE)
		y -= h / 2;
	else if (jv == MMG_JUST_BOTTOM)
		y -= h;
	/* text, not s: s points at the length byte, and drawing that as a
	   character puts a blank cell in front of every string. */
	mm_gtext(x, y, font, scale, fc, bc, text, len);
}

/*
 *	--- MAP presets --------------------------------------------------
 *
 *	MAP MAXIMITE and MAP GRAYSCALE are sixteen colours each and then a
 *	MAP SET, so they belong here rather than in the kernel: a program
 *	that does not use them carries neither.
 */

/*
 *	MMBasic's CMM1map (Draw.c) - the original Colour Maximite's
 *	sixteen, which are NOT the RGB121 cube: eight saturated colours
 *	first, then eight darker ones, so colour 1 is blue rather than the
 *	cube's blue.  A program written for the Maximite expects that
 *	order.
 */
static void mmg_map_maximite(void)
{
	static const long m[16] = {
		0x000000L,		/* BLACK    */
		0x0000FFL,		/* BLUE     */
		0x00FF00L,		/* GREEN    */
		0x00FFFFL,		/* CYAN     */
		0xFF0000L,		/* RED      */
		0xFF00FFL,		/* MAGENTA  */
		0xFFFF00L,		/* YELLOW   */
		0xFFFFFFL,		/* WHITE    */
		0x004000L,		/* MYRTLE   */
		0x0040FFL,		/* COBALT   */
		0x008000L,		/* MIDGREEN */
		0x0080FFL,		/* CERULEAN */
		0xFF4000L,		/* RUST     */
		0xFF40FFL,		/* FUCHSIA  */
		0xFF8000L,		/* BROWN    */
		0xFF80FFL		/* LILAC    */
	};
	int i;

	for (i = 0; i < 16; i++)
		mm_map(i, m[i]);
	mm_map_set();
}

/*
 *	Sixteen greys.  MMBasic computes j = i*16 - (16 - i + 1) over
 *	i = 1..16, which is 17*(i-1) - so the steps are 0, 17, 34 ... 255,
 *	an exact ramp reaching both ends.  Written that way here.
 */
static void mmg_map_greyscale(void)
{
	int i, j;

	for (i = 0; i < 16; i++) {
		j = 17 * i;
		mm_map(i, ((long)j << 16) | ((long)j << 8) | (long)j);
	}
	mm_map_set();
}

#endif /* MMB_GFX_H */

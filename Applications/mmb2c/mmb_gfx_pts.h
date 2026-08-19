#ifndef MMB_GFX_PTS_H
#define MMB_GFX_PTS_H
/*
 *	The batch helpers every span/point primitive is built from, and
 *	the reason the drawing primitives live in headers at all.
 *
 *	These are NOT in bcrun. bcrun is loaded for every translated
 *	program and shares one process with the program's own code and
 *	data, so a byte added there is a byte taken from every program on
 *	the machine - including the ones that draw nothing. Here they cost
 *	only the programs that use them, and only while that program is
 *	the resident one.
 *
 *	Every function is static, and cc1 generates nothing for a file
 *	scope static that nothing else names (hosttest/deadstatic.sh).
 *	That rule counts names, not reachability: a static that names
 *	itself, or that a dead static names, survives.  Which is why there
 *	is one header PER PRIMITIVE - mmb_gfx_circle.h, mmb_gfx_text.h,
 *	mmb_gfx_map.h - and the translator includes exactly the ones the
 *	program uses.  Inside a header everything is reachable from its
 *	entry point, so nothing is carried that is not needed.  There is
 *	no linker on this target to do any of this afterwards, which is
 *	why the compiler learned to do it.
 *
 *	The only things reaching outside are mm_plot and mm_fill - a run
 *	of points and a run of rectangles - so however many primitives
 *	end up in headers, the kernel and bcrun stay the size they are.
 *
 *	The algorithms are MMBasic's own, from Draw.c, so that a
 *	translated program draws the same pixels as the interpreter. That
 *	matters for more than looks: comparing pixel counts against
 *	MMBasic is how a code generation bug was found here once already.
 */

#include "mmb_runtime.h"

/*
 *	The helpers below are shared by several primitives, so any one
 *	primitive's include leaves some of them uncalled.  cc1 generates
 *	nothing for those; gcc generates nothing either but says so, and
 *	the attribute quiets it.  MM_FCC keeps the attribute away from
 *	the fcc pipeline, whose cc0 does not read attributes.
 */
#if !defined(MM_FCC) && defined(__GNUC__)
#define MMG_UNUSED	__attribute__((unused))
#else
#define MMG_UNUSED
#endif

/*
 *	Items buffered before crossing into the kernel. Points cost two
 *	shorts and rectangles four, so this is 128 and 256 bytes of the
 *	caller's stack; both live in the primitive's own frame rather
 *	than in static storage, so an unused header costs no BSS either.
 */
#define MMG_BATCH	32

static MMG_UNUSED void mmg_pt(short *b, int *n, MMINTEGER c, int x, int y)
{
	b[*n * 2] = (short)x;
	b[*n * 2 + 1] = (short)y;
	if (++*n == MMG_BATCH) {
		mm_plot(b, *n, c);
		*n = 0;
	}
}

static MMG_UNUSED void mmg_rc(short *b, int *n, MMINTEGER c, int x1, int y1,
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
 *	The same, normalising first.  MMBasic's DrawRectangle accepts its
 *	corners in either order - DrawRBox hands it right-to-left spans -
 *	and normalises inside; the batched crossing does not, so any
 *	caller reproducing firmware geometry goes through this.
 */
static MMG_UNUSED void mmg_rectn(short *b, int *n, MMINTEGER c, int x1, int y1,
		      int x2, int y2)
{
	int t;

	if (x2 < x1) { t = x1; x1 = x2; x2 = t; }
	if (y2 < y1) { t = y1; y1 = y2; y2 = t; }
	mmg_rc(b, n, c, x1, y1, x2, y2);
}

/*
 *	One rectangle, one crossing.  For the primitives that draw a
 *	handful of rectangles in different colours (BOX is at most five),
 *	batching per colour costs more code than it saves syscalls.
 */
static MMG_UNUSED void mmg_rect1(MMINTEGER c, int x1, int y1, int x2, int y2)
{
	short r[4];
	int t;

	if (x2 < x1) { t = x1; x1 = x2; x2 = t; }
	if (y2 < y1) { t = y1; y1 = y2; y2 = t; }
	r[0] = (short)x1;
	r[1] = (short)y1;
	r[2] = (short)x2;
	r[3] = (short)y2;
	mm_fill(r, 1, c);
}

#endif /* MMB_GFX_PTS_H */

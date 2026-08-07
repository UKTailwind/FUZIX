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

#endif /* MMB_GFX_PTS_H */

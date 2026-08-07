#ifndef MMB_GFX_MAP_H
#define MMB_GFX_MAP_H
/*
 *	MAP MAXIMITE and MAP GRAYSCALE are sixteen colours each and then a
 *	MAP SET, so they belong here rather than in the kernel: a program
 *	that does not use them carries neither.  See mmb_gfx_pts.h for
 *	why the primitives live in headers, one per primitive.
 */

#include "mmb_runtime.h"

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

#endif /* MMB_GFX_MAP_H */

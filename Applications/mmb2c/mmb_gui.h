#ifndef MMB_GUI_H
#define MMB_GUI_H
/*
 *	GUI BITMAP x, y, bits [,width] [,height] [,scale] [,c] [,bc]
 *
 *	See mmb_gfx_pts.h for why the primitives live in headers, one per
 *	primitive.
 *
 *	MMBasic's cmd_guiMX170 + DrawBitmap (Draw.c:443, :10440).  Only
 *	the BITMAP form of GUI is here: the rest of that command is
 *	touch-screen widgets, which this machine has no hardware for.
 *
 *	THE BIT ORDER, which is the part worth reading.
 *
 *	The manual says "the first byte as the first bits of the top line
 *	(bit 7 first, then bit 6, etc)".  The firmware actually says, in
 *	all five of its DrawBitmap variants and identically:
 *
 *	    bitmap[n / 8] >> (((height * width) - n - 1) % 8) & 1
 *	    where n = row * width + column
 *
 *	Those two agree ONLY WHEN height*width IS A MULTIPLE OF 8 - and
 *	then exactly, because (T - n - 1) % 8 == 7 - (n % 8) when 8 | T.
 *	When it is not, the bit picked inside each byte is rotated by
 *	T mod 8 and the manual's description is simply wrong.  Copied
 *	verbatim rather than corrected: a program drawn against the
 *	firmware has to draw the same here, and "the same" includes this.
 *
 *	NOT the font packing, though it looks like it.  A font row is
 *	padded to a byte boundary; this is one continuous bitstream, so
 *	with a width that is not a multiple of 8 the next row starts
 *	mid-byte.  For a 16x8 sprite - which is what the Game*Mite
 *	programs use - the two are the same and neither quirk shows.
 *
 *	AND THE INTEGER FORM IS LITTLE-ENDIAN.  MMBasic passes an integer
 *	bitmap as `s = (unsigned char *)&i64`, so the FIRST byte drawn is
 *	the LOW byte of the value: &h00000000000000FF is the top line,
 *	not &hFF00000000000000.  Written out by shifts below rather than
 *	by casting an address, so it does not depend on the host.
 */

#include "mmb_gfx_pts.h"

/*
 *	MMBasic plots every scaled pixel on its own, four loops deep.
 *	The same pixels come out of one background rectangle plus a run
 *	of foreground rectangles, and a batch carries ONE colour
 *	(mmg_rc flushes with it), so the two colours are two passes:
 *
 *	  - the background, if it is not transparent, as a single
 *	    rectangle covering the whole scaled bitmap.  Every clear
 *	    pixel gets bc that way, which is what the firmware's
 *	    `else if (bc >= 0)` does one pixel at a time;
 *	  - then the set bits, as runs of adjacent columns, drawn over
 *	    it.  Foreground last, so where they overlap fg wins - the
 *	    same order as the firmware's if/else.
 *
 *	A 16x8 sprite is one rectangle plus a dozen, not 128 points.
 */
static void mmg_gui_bitmap(int x1, int y1, const unsigned char *bits,
			   MMINTEGER nbytes, int w, int h, int scale,
			   MMINTEGER fc, MMINTEGER bc)
{
	short buf[MMG_BATCH * 4];
	int nb = 0;
	int i, k, total, run0 = 0, inrun = 0;

	/* cmd_guiMX170's own ranges, which are hard errors there and not
	   clamps.  fc and bc are checked by the caller against the
	   colour range; w, h and scale are checked here. */
	if (w < 1 || w > (int)mm_hres())
		MM_RAISE("Invalid bitmap width");
	if (h < 1 || h > (int)mm_vres())
		MM_RAISE("Invalid bitmap height");
	if (scale < 1 || scale > 15)
		MM_RAISE("Invalid scale");
	total = h * w;
	if ((MMINTEGER)total > nbytes * 8)
		MM_RAISE("Not enough data");

	/* DrawBitmap's early-out, before anything is drawn. */
	if (x1 >= (int)mm_hres() || y1 >= (int)mm_vres()
	    || x1 + w * scale < 0 || y1 + h * scale < 0)
		return;

	if (bc >= 0)
		mmg_rect1(bc, x1, y1,
			  x1 + w * scale - 1, y1 + h * scale - 1);

	for (i = 0; i < h; i++) {
		for (k = 0; k <= w; k++) {
			int set = 0;
			if (k < w) {
				int n = i * w + k;
				set = (bits[n / 8]
				       >> ((total - n - 1) % 8)) & 1;
			}
			if (set && !inrun) {
				run0 = k;
				inrun = 1;
			} else if (!set && inrun) {
				mmg_rc(buf, &nb, fc,
				       x1 + run0 * scale,
				       y1 + i * scale,
				       x1 + k * scale - 1,
				       y1 + (i + 1) * scale - 1);
				inrun = 0;
			}
		}
	}
	if (nb)
		mm_fill(buf, nb, fc);
}

/*
 *	The integer form.  Eight bytes, low byte first - see the header
 *	comment; an integer bitmap therefore holds at most 64 bits, which
 *	is MMBasic's `bytes = 8` and its "Not enough data" above.
 */
static MMG_UNUSED void mmg_gui_bitmap_i(int x1, int y1, MMINTEGER v, int w,
					int h, int scale, MMINTEGER fc,
					MMINTEGER bc)
{
	unsigned char b[8];
	int i;

	for (i = 0; i < 8; i++)
		b[i] = (unsigned char)((v >> (i * 8)) & 0xFF);
	mmg_gui_bitmap(x1, y1, b, 8, w, h, scale, fc, bc);
}

#endif /* MMB_GUI_H */

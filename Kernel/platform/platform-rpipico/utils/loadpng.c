/* loadpng - draw a PNG on the screen.
 *
 *   loadpng file.png [x [y [transparent [cutoff]]]]
 *
 * MMBasic's LOAD PNG, as a program, on the same terms as loadimage and
 * loadjpg: the interpreter hands /usr/bin/loadpng an argv and waits.
 * The decoder is MMBasic's own upng (third_party_mod/upng.c) with two
 * lines changed - see upng_pc3.h.
 *
 * WHY THIS ONE NEEDS THE PSRAM ARENA.  picojpeg decodes an MCU at a
 * time; upng cannot.  PNG's filters refer to the row above and its
 * DEFLATE stream is one continuous window over the whole image, so the
 * entire picture is inflated before a pixel of it can be drawn: about
 * 307K of filtered raster and another 307K of RGBA8 for a full 320x240
 * screen, against an 84-block (336K) process pool.  That is why the
 * reference guards LOAD PNG with `#ifdef rp2350` and does not offer it
 * at all on a board without PSRAM.  Here the big blocks come out of a
 * PSRAM arena and cost the pool nothing.
 *
 * TRANSPARENCY, and one simplification the reference cannot make.
 * MMBasic composites: where alpha is below the cutoff it reads the
 * SCREEN back into a buffer, substitutes those pixels, and draws the
 * whole rectangle.  It has to, because DrawBuffer writes a rectangle
 * wholesale.  We draw pixel by pixel through GFXIOC_PIXELS, so a
 * transparent pixel is simply NOT DRAWN - the screen keeps what it had,
 * which is the same result with no readback and no second buffer.
 *
 *	transparent = -1	leave the screen showing (MMBasic's -1)
 *	transparent = 0-15	paint that palette colour (0 is the
 *				default, and MMBasic's default too)
 *	cutoff			alpha above which a pixel is opaque,
 *				1-254, default 20 as in the reference
 *
 * The colour of palette index n is the exact inverse of loadimage's
 * quantiser - RGB121, bit 3 red, bits 2-1 green, bit 0 blue - which is
 * how MMBasic's RGB121map is built (MYRTLE, 0b0010, is RGB(0,64,0)).
 *
 * ---- SPRITE MODE -----------------------------------------------------
 *
 *   loadpng -s file.png [transparent [cutoff]]
 *
 * is MMBasic's SPRITE LOADPNG, and writes the decoded sprite to STDOUT
 * rather than to the screen, because the sprite belongs to the BASIC
 * program and this is a different process.  The runtime reads it down a
 * pipe straight into the sprite's buffer (mms_loadpng in mmb_sprite.h).
 *
 *	uint16 w, uint16 h, little endian
 *	then w*h bytes, ONE COLOUR INDEX PER BYTE, row major
 *
 * One byte per pixel and not the reference's packed 4bpp, because our
 * sprite buffers are one index per byte (mms_alloc: sb->img) - the same
 * deliberate divergence the blit buffers make, recorded in mmb_blit.h.
 * Packing here would only mean unpacking at the other end.
 *
 * `transparent` carries the reference's sign trick: 0-15 is the index
 * given to pixels below the cutoff, and a NEGATIVE value means "-n is
 * the index to substitute for opaque BLACK" (so artwork whose black
 * would vanish against a black background can be remapped), with the
 * transparent index then 0.  cutoff defaults to 30 here, not 20 - the
 * reference uses a different default for the sprite form.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "upng.h"

#define GFXIOC_INFO	0x000E
#define GFXIOC_PIXELS	0x0014

struct gfx_info {
	unsigned short width, height, stride;
	unsigned char bpp, mode;
};

struct gfx_pt { short x, y; };

struct gfx_batch {
	unsigned short count;
	unsigned short flags;
	void *items;
	void *colours;
};

#define BATCH	256

static int sysfd;
static struct gfx_info gi;

static struct gfx_pt  pts[BATCH];
static unsigned long  cols[BATCH];
static int npts;

/* upng_pc3.h owns the arena; loadpng borrows it for the file itself. */
#include "upng_pc3.h"

static void die(const char *what)
{
	fprintf(stderr, "loadpng: %s\n", what);
	exit(1);
}

/* ---- output --------------------------------------------------------- */

static void flush(void)
{
	struct gfx_batch b;

	if (npts == 0)
		return;
	b.count = (unsigned short)npts;
	b.flags = 0;
	b.items = pts;
	b.colours = cols;
	ioctl(sysfd, GFXIOC_PIXELS, &b);
	npts = 0;
}

static void plot(int x, int y, unsigned long rgb)
{
	pts[npts].x = (short)x;
	pts[npts].y = (short)y;
	cols[npts] = rgb;
	if (++npts == BATCH)
		flush();
}

/* loadimage.c's quantiser, unchanged: RGB121, or ink and paper. */
static unsigned long quantise(int r, int g, int b)
{
	int r1 = (r >= 128) ? 1 : 0;
	int g2 = (g * 3 + 127) / 255;
	int b1 = (b >= 128) ? 1 : 0;

	return ((unsigned long)(r1 * 255) << 16)
	     | ((unsigned long)(g2 == 0 ? 0 : g2 == 1 ? 0x40
				: g2 == 2 ? 0x80 : 0xFF) << 8)
	     |  (unsigned long)(b1 * 255);
}

static unsigned long tocolour(int r, int g, int b)
{
	if (gi.bpp == 1) {
		int y = (r * 77 + g * 151 + b * 28) >> 8;

		return (y >= 128) ? 0xFFFFFFUL : 0UL;
	}
	return quantise(r, g, b);
}

/*
 * Palette index -> RGB888, the inverse of the quantiser above.  This is
 * MMBasic's RGB121map without the table: index bit 3 is red, bits 2-1
 * are green, bit 0 is blue, and green's four levels are 0, 0x40, 0x80,
 * 0xFF.  Checked against the reference's own constants - MYRTLE 0b0010
 * is RGB(0,64,0), RUST 0b1010 is RGB(255,64,0), LILAC 0b1101 is
 * RGB(255,128,255).
 */
static unsigned long palcolour(int n)
{
	int g = (n >> 1) & 3;

	return ((unsigned long)((n & 8) ? 255 : 0) << 16)
	     | ((unsigned long)(g == 0 ? 0 : g == 1 ? 0x40
				: g == 2 ? 0x80 : 0xFF) << 8)
	     |  (unsigned long)((n & 1) ? 255 : 0);
}

/*
 * The 4-bit index for a colour, the reference's own expression from
 * SPRITE LOADPNG: red's top bit, green's top two, blue's top bit.
 * mms_loadarray in mmb_sprite.h derives the same index the same way.
 */
static unsigned char index4(int r, int g, int b)
{
	return (unsigned char)(((r & 0x80) >> 4) | ((g & 0xC0) >> 5)
			       | ((b & 0x80) >> 7));
}

/*
 * Sprite mode: the picture down stdout, for the runtime to read into a
 * sprite buffer.  Nothing is drawn and the display is never opened -
 * this runs happily with no graphics mode set.
 */
static int sprite_out(const unsigned char *px, int w, int h,
		      int transparent, int remap, int cutoff)
{
	unsigned char hdr[4], *row;
	int x, y;

	hdr[0] = (unsigned char)(w & 0xFF);
	hdr[1] = (unsigned char)((w >> 8) & 0xFF);
	hdr[2] = (unsigned char)(h & 0xFF);
	hdr[3] = (unsigned char)((h >> 8) & 0xFF);
	if (write(1, hdr, 4) != 4)
		die("cannot write the sprite");

	row = (unsigned char *)GetMemory((unsigned long)w);
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++, px += 4) {
			unsigned char c;

			if (px[3] > cutoff) {
				c = index4(px[0], px[1], px[2]);
				/* opaque black -> the remap index, so
				   black artwork survives a black
				   background.  Only for OPAQUE pixels:
				   a transparent one keeps its own
				   index. */
				if (remap >= 0 && c == 0)
					c = (unsigned char)remap;
			} else
				c = (unsigned char)transparent;
			row[x] = c;
		}
		if (write(1, row, w) != w)
			die("cannot write the sprite");
	}
	return 0;
}

/* ---- main ----------------------------------------------------------- */

int main(int argc, char **argv)
{
	upng_t *upng;
	const unsigned char *px;
	unsigned char *file;
	int xorg = 0, yorg = 0, transparent = 0, cutoff = 20;
	int w, h, x, y, fd, sprite = 0, remap = -1;
	long size;
	char **av = argv;
	int ac = argc;

	/* -s: the sprite form.  Its arguments are different and its
	   defaults are the reference's for SPRITE LOADPNG, not for
	   LOAD PNG - cutoff 30 rather than 20. */
	if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 's'
	    && argv[1][2] == 0) {
		sprite = 1;
		cutoff = 30;
		av = argv + 1;
		ac = argc - 1;
	}

	if (ac < 2) {
		fprintf(stderr, "usage: loadpng file.png "
				"[x [y [transparent [cutoff]]]]\n"
				"       loadpng -s file.png "
				"[transparent [cutoff]]   (sprite, to stdout)\n");
		return 1;
	}
	if (sprite) {
		if (ac > 2) transparent = atoi(av[2]);
		if (ac > 3) cutoff = atoi(av[3]);
		/* MMBasic's sign trick: -n means "substitute index n for
		   opaque black", and the transparent index is then 0. */
		if (transparent < 0) {
			remap = -transparent;
			transparent = 0;
		}
		if (transparent > 15 || remap > 15)
			die("transparent colour must be -15 to 15");
	} else {
		if (ac > 2) xorg = atoi(av[2]);
		if (ac > 3) yorg = atoi(av[3]);
		if (ac > 4) transparent = atoi(av[4]);
		if (ac > 5) cutoff = atoi(av[5]);
		if (transparent < -1 || transparent > 15)
			die("transparent colour must be -1 to 15");
	}
	if (cutoff < 1 || cutoff > 254)
		die("cutoff must be 1 to 254");

	/* A sprite needs no display: it is data, and the program asking
	   for it may not have set a mode yet. */
	if (!sprite) {
		sysfd = open("/dev/sys", O_RDWR);
		if (sysfd < 0)
			die("cannot open /dev/sys");
		if (ioctl(sysfd, GFXIOC_INFO, &gi) < 0)
			die("no display");
	}

	/* The file, whole, in the arena: upng wants it as one block. */
	fd = open(av[1], O_RDONLY);
	if (fd < 0)
		die("cannot open the file");
	size = lseek(fd, 0L, 2);
	if (size <= 0 || lseek(fd, 0L, 0) < 0)
		die("cannot read the file");
	file = (unsigned char *)GetMemory((unsigned long)size);
	{
		long got = 0;

		while (got < size) {
			int n = read(fd, file + got, (int)(size - got));

			if (n <= 0)
				die("truncated file");
			got += n;
		}
	}
	close(fd);

	upng = upng_new_from_bytes(file, (unsigned long)size);
	if (upng == NULL)
		die("not enough memory for the image");
	if (upng_header(upng) != UPNG_EOK)
		die("not a PNG, or a PNG this decoder cannot read");
	w = (int)upng_get_width(upng);
	h = (int)upng_get_height(upng);
	/* The reference's own bound, and its wording.  A sprite is not
	   bounded by the screen: it is a buffer, and may be shown
	   anywhere or clipped when it is. */
	if (!sprite && (w + xorg > gi.width || h + yorg > gi.height))
		die("Image too large");
	if (upng_decode(upng) != UPNG_EOK)
		die("the PNG could not be decoded");
	if (upng_get_format(upng) != UPNG_RGBA8)
		die("Invalid format, must be RGBA8888 or indexed PNG");

	px = upng_get_buffer(upng);
	if (sprite)
		return sprite_out(px, w, h, transparent, remap, cutoff);
	for (y = 0; y < h; y++) {
		int sy = yorg + y;

		if (sy < 0 || sy >= gi.height) {
			px += (unsigned long)w * 4;
			continue;
		}
		for (x = 0; x < w; x++, px += 4) {
			int sx = xorg + x;

			if (sx < 0 || sx >= gi.width)
				continue;
			if (px[3] > cutoff)
				plot(sx, sy, tocolour(px[0], px[1], px[2]));
			else if (transparent >= 0)
				plot(sx, sy, palcolour(transparent));
			/* else: leave the screen showing through */
		}
		flush();
	}
	flush();
	return 0;
}

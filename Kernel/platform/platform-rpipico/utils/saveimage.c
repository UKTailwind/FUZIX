/* saveimage - write the screen to a Windows BMP or a PNG file.
 *
 *   saveimage [-p] file[.bmp|.png] [x y w h]
 *   saveimage -s [-p] [dir]
 *
 * MMBasic's SAVE IMAGE (and SAVE COMPRESSED IMAGE, the same command
 * since PicoMite 2abbb53), SAVE PNG with -p, and Print Screen's save
 * with -s, as a program rather than as firmware.  Nothing about it
 * needs to be inside bcrun or the kernel: it runs once, it is not in
 * anybody's inner loop, and as a separate binary it is cross-compiled
 * with gcc rather than by the compiler on the board.  It costs a BASIC
 * program nothing at all - not a byte of the 48K that program has to
 * live in - and it works from the shell on its own.
 *
 * THE BMP is PicoMite's (misc/FileIO.c SaveImageRLE, 2026-10-26): the
 * area's colours are counted first, and 16 or fewer make a 4-bit file
 * and up to 256 an 8-bit one, each with a palette of only the colours
 * used - in the order they are first met, from the top left - and RLE
 * compression (BI_RLE4, BI_RLE8, which loadimage reads).  Every screen
 * here has 256 colours or fewer: 16 in the 4-bit modes, two in MODE 1,
 * and on the console at most the 256 of RGB332.  So the 24-bit file
 * this always wrote now comes only from a picture with more, which
 * nothing here can draw.  A whole MODE 2 screen was 230K, and MODE 3's
 * 921K; as RLE, a typical one is a few K.
 *
 * THE PNG (-p) is PicoMite's graphics/PngEncoder.c: 8-bit RGB, each
 * row with the filter whose output has the smallest sum of absolute
 * values, as signed bytes, and the rows one zlib stream - a single
 * deflate block of the fixed Huffman codes, its matches found through a
 * hash of the next three bytes within a 16K ring.  ".png" is added to a
 * name without an extension, as ".bmp" is for SAVE IMAGE.
 *
 * PRINT SCREEN (-s) saves the whole screen under PicoMite's name for
 * it, SCREEN_yyyymmdd_hhmmss.BMP (.PNG with -p), in dir - the current
 * directory if none is given, which is what "A:/" is to a translated
 * program.  A name already taken (two saves in one second) gets _1,
 * _2 ... up to _99, rather than being overwritten.  The date and time
 * are DATE$'s and TIME$'s: time() taken as the local clock, with no
 * time zone (mmb_datetime.h), so gmtime.  Nothing is printed when it
 * works: the editor calls it with its own screen showing.
 *
 * The screen is read a band of rows at a time through GFXIOC_BLITRDR,
 * the rows-out reader: the bytes of the framebuffer as they lie, 4-bit
 * or 1-bit, which is what a 320x240 screen costs eight ioctls rather
 * than the 76,800 GETPIXELs this used to make - a tenth of a second on
 * the board, and on a PC, where every ioctl is a round trip to the
 * device server, several seconds.  A pixel's colour comes from a cache
 * keyed on what decides it: the index in a graphics mode, and on the
 * console the cell and the bit, since each 8x12 cell there has its own
 * ink and paper (display.c's disp_tile_fg and _bg).  A key's colour is
 * asked of the kernel with GFXIOC_GETPIXEL at the first pixel that
 * carries it, so the kernel resolves the live palette exactly as it did
 * for every pixel before: at most 16 calls in a graphics mode, and 6400
 * on the console against its 307,200 pixels.  That matters twice over
 * now, since the BMP reads the screen twice: once for the colours and
 * once for the rows.
 *
 * A kernel older than BLITRDR refuses the first read, and a console of
 * another size has cells this does not know; both fall back to
 * GETPIXEL for every pixel, which is what this always did.
 *
 * The headers are written by hand rather than through a struct: they
 * are defined as byte offsets, little-endian in a BMP and big-endian in
 * a PNG, and a struct would be at the mercy of the compiler's padding.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "pc3sys.h"

#define GFXIOC_INFO	0x000E
#define GFXIOC_GETPIXEL	0x0011
#define GFXIOC_BLITRDR	0x003A
#define GFX_PACK(x, y)	(((x) & 0x3FF) | (((y) & 0x1FF) << 10))

struct gfx_info {
	unsigned short width, height, stride;
	unsigned char bpp, mode;
};

/* pico_ioctl.h's gfx_blitr: rows OUT of the target, rows * len bytes
 * contiguous in buf, each row stride bytes after the last. */
struct gfx_blitr {
	uint32_t offset;
	uint16_t len, rows, stride, pad;
	void *buf;
};

/* The widest the hardware can be. */
#define MAXW	640

/* Rows per BLITRDR: 32 rows of a 320-byte stride is 10K, and eight
   calls for a 240-line mode. */
#define BAND	32

/* The console: display.h's DISP_WIDTH x DISP_HEIGHT in DISP_CELL_W x
   DISP_CELL_H cells, each with its own ink and paper. */
#define CON_W	640
#define CON_H	480
#define CELL_W	8
#define CELL_H	12

static int sysfd;
static struct gfx_info gi;
static int x0, y0, w, h;

static void put16(unsigned char *p, unsigned v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
}

static void put32(unsigned char *p, unsigned long v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);
	p[3] = (unsigned char)(v >> 24);
}

/* ---- the file -------------------------------------------------------- */

static int ofd;
static const char *oname;
static unsigned char obuf[4096];
static int on;

static void wfail(void)
{
	perror(oname);
	exit(1);
}

static void oflush(void)
{
	if (on && write(ofd, obuf, on) != on)
		wfail();
	on = 0;
}

static void put(const void *p, int n)
{
	const unsigned char *s = p;

	while (n > 0) {
		int k = (int)sizeof obuf - on;

		if (k > n)
			k = n;
		memcpy(obuf + on, s, k);
		on += k;
		s += k;
		n -= k;
		if (on == (int)sizeof obuf)
			oflush();
	}
}

/* Four bytes at offset off, once the rest is written: a size only
   known at the end. */
static void patch32(long off, unsigned long v)
{
	unsigned char q[4];

	oflush();
	put32(q, v);
	if (lseek(ofd, off, 0) < 0 || write(ofd, q, 4) != 4)
		wfail();
}

/* ---- the screen ------------------------------------------------------ */

static unsigned char *band;
static int bx0, blen, btop, bn;	/* the band holds rows btop..btop+bn-1 */
static int rowsout, console;
static long *kcol;		/* each key's colour, -1 until asked */

static long getpixel(int x, int y)
{
	int c = pc3_ioctl(sysfd, GFXIOC_GETPIXEL, (void *)(uintptr_t)GFX_PACK(x, y));

	return c < 0 ? 0 : c;		/* off-screen reads black */
}

/* The rows-out reader and the key cache, where the mode has them.  The
 * bytes BLITRDR gives are those that hold pixels x0..x0+w-1 of a row,
 * in the board's packing whatever the machine (pc3d turns its own):
 * the high nibble the left pixel at 4bpp, bit 7 the left pixel at
 * 1bpp. */
static void reader_init(void)
{
	int nkeys, shift, i;

	if (!gi.stride)
		return;
	if (gi.mode != 0xFF && (gi.bpp == 4 || gi.bpp == 1)) {
		nkeys = 1 << gi.bpp;
	} else if (gi.mode == 0xFF && gi.bpp == 1
		   && gi.width == CON_W && gi.height == CON_H) {
		nkeys = 2 * (CON_W / CELL_W) * (CON_H / CELL_H);
		console = 1;
	} else {
		return;
	}
	shift = (gi.bpp == 4) ? 1 : 3;
	bx0 = x0 >> shift;
	blen = ((x0 + w - 1) >> shift) - bx0 + 1;
	band = malloc((size_t)BAND * blen);
	kcol = malloc((size_t)nkeys * sizeof(long));
	if (!band || !kcol)
		return;
	for (i = 0; i < nkeys; i++)
		kcol[i] = -1;
	rowsout = 1;
}

/* Make row r readable: a band from r down when the pass goes down the
   screen, or one ending at r when it goes up. */
static void need_row(int r, int down)
{
	struct gfx_blitr gr;
	int top, n;

	if (!rowsout || (r >= btop && r < btop + bn))
		return;
	if (down) {
		top = r;
		n = y0 + h - r;
		if (n > BAND)
			n = BAND;
	} else {
		top = r - BAND + 1;
		if (top < y0)
			top = y0;
		n = r - top + 1;
	}
	gr.offset = (uint32_t)top * gi.stride + (uint32_t)bx0;
	gr.len = (uint16_t)blen;
	gr.rows = (uint16_t)n;
	gr.stride = gi.stride;
	gr.pad = 0;
	gr.buf = band;
	if (pc3_ioctl(sysfd, GFXIOC_BLITRDR, &gr) < 0) {
		rowsout = 0;		/* an older kernel: the slow way */
		return;
	}
	btop = top;
	bn = n;
}

/* Pixel x of row r as 0xRRGGBB, once need_row(r) has been called. */
static long pixel(int x, int r)
{
	const unsigned char *src;
	unsigned v;
	int key;

	if (!rowsout)
		return getpixel(x, r);
	src = band + (r - btop) * blen;
	if (gi.bpp == 4) {
		v = src[(x >> 1) - bx0];
		key = (x & 1) ? (int)(v & 15) : (int)(v >> 4);
	} else {
		v = src[(x >> 3) - bx0];
		key = (int)(v >> (7 - (x & 7))) & 1;
		if (console)
			key += 2 * ((r / CELL_H) * (CON_W / CELL_W) + x / CELL_W);
	}
	if (kcol[key] < 0)
		kcol[key] = getpixel(x, r);
	return kcol[key];
}

/* Row r as blue, green, red, three bytes a pixel: the 24-bit BMP's
   order, and the order PicoMite's PngRow takes. */
static void row_bgr(int r, unsigned char *p)
{
	int x;

	for (x = x0; x < x0 + w; x++, p += 3) {
		long c = pixel(x, r);

		p[0] = (unsigned char)c;
		p[1] = (unsigned char)(c >> 8);
		p[2] = (unsigned char)(c >> 16);
	}
}

/* ---- BMP ------------------------------------------------------------- */

/* The colours, PicoMite's savepal_t: 0xRRGGBB, and their hash table of
   indexes into pal, -1 for none. */
#define SLOTS	512
static unsigned long pal[256];
static short slot[SLOTS];
static int npal;

/* Colour c's index in the palette, added if it is new (add); -1 when
   it is not there and cannot be (256 already). */
static int palindex(unsigned long c, int add)
{
	unsigned hs = (unsigned)((uint32_t)c * 2654435761u) >> 23;

	while (slot[hs] >= 0) {
		if (pal[slot[hs]] == c)
			return slot[hs];
		hs = (hs + 1) & (SLOTS - 1);
	}
	if (!add || npal == 256)
		return -1;
	pal[npal] = c;
	slot[hs] = (short)npal;
	return npal++;
}

/* A row of palette indexes as BMP RLE, PicoMite's SaveRleRow: a run of
 * one index as its count and the index (RLE4: the index in both
 * nibbles), three or more that are not a run as 0, n and the indexes
 * (absolute mode, padded to a word), and 0, 0 at the end of the row. */
static int rle_row(const unsigned char *ix, int n, unsigned char *o, int bits)
{
	int i = 0, k, s, b, len = 0;

	while (i < n) {
		int run = 1;

		while (i + run < n && run < 255 && ix[i + run] == ix[i])
			run++;
		if (run >= 2) {
			o[len++] = (unsigned char)run;
			o[len++] = (unsigned char)(bits == 4 ? (ix[i] << 4 | ix[i]) : ix[i]);
			i += run;
			continue;
		}
		s = i;
		k = 0;
		/* up to the start of a run of three */
		while (i < n && k < 255
		       && !(i + 2 < n && ix[i] == ix[i + 1] && ix[i] == ix[i + 2])) {
			i++;
			k++;
		}
		/* RLE4 absolute mode in whole bytes, an even count (Pillow
		   before 13.0 reads no other): the last pixel goes back, to
		   be a run */
		if (bits == 4 && k >= 3 && (k & 1)) {
			i--;
			k--;
		}
		if (k < 3) {		/* too few for absolute mode */
			while (s < i) {
				o[len++] = 1;
				o[len++] = (unsigned char)(bits == 4 ? ix[s] << 4 : ix[s]);
				s++;
			}
			continue;
		}
		o[len++] = 0;
		o[len++] = (unsigned char)k;
		if (bits == 4) {
			for (b = 0; b < k / 2; b++)
				o[len++] = (unsigned char)(ix[s + 2 * b] << 4 | ix[s + 2 * b + 1]);
			if ((k / 2) & 1)
				o[len++] = 0;
		} else {
			while (s < i)
				o[len++] = ix[s++];
			if (k & 1)
				o[len++] = 0;
		}
	}
	o[len++] = 0;
	o[len++] = 0;
	return len;
}

/* The area with a palette and RLE, PicoMite's SaveImageRLE; 0, with
 * nothing written, when it has more than 256 colours.  The screen is
 * read twice, for the colours and then for the rows; the file's size
 * and the pixels' are written last, when they are known. */
static int save_rle(void)
{
	static unsigned char ix[MAXW], o[MAXW * 2 + 16];
	unsigned char hdr[54], q[4];
	unsigned long size = 0;
	int x, r, bits, n, i;

	memset(slot, 0xFF, sizeof slot);
	npal = 0;
	for (r = y0; r < y0 + h; r++) {		/* given up at the 257th */
		need_row(r, 1);
		for (x = x0; x < x0 + w; x++)
			if (palindex(pixel(x, r), 1) < 0)
				return 0;
	}
	bits = npal <= 16 ? 4 : 8;
	memset(hdr, 0, sizeof hdr);
	hdr[0] = 'B'; hdr[1] = 'M';
	put32(hdr + 10, 54 + 4 * npal);	/* where the pixels start */
	put32(hdr + 14, 40);		/* BITMAPINFOHEADER */
	put32(hdr + 18, (unsigned long)w);
	put32(hdr + 22, (unsigned long)h);
	put16(hdr + 26, 1);		/* planes */
	put16(hdr + 28, bits);
	put32(hdr + 30, bits == 4 ? 2 : 1);	/* BI_RLE4, BI_RLE8 */
	put32(hdr + 38, 2835);		/* 72 dpi, in pixels per metre */
	put32(hdr + 42, 2835);
	put32(hdr + 46, (unsigned long)npal);	/* the colours used */
	put(hdr, sizeof hdr);
	for (i = 0; i < npal; i++) {
		put32(q, pal[i]);	/* blue, green, red, 0 */
		put(q, 4);
	}
	for (r = y0 + h - 1; r >= y0; r--) {	/* a BMP's rows from the bottom */
		need_row(r, 0);
		for (x = 0; x < w; x++) {
			int c = palindex(pixel(x0 + x, r), 0);

			/* a colour drawn between the passes is saved as the first */
			ix[x] = (unsigned char)(c < 0 ? 0 : c);
		}
		n = rle_row(ix, w, o, bits);
		if (r == y0) {			/* the end of the picture */
			o[n++] = 0;
			o[n++] = 1;
		}
		put(o, n);
		size += (unsigned long)n;
	}
	patch32(2, 54 + 4 * (unsigned long)npal + size);	/* the file */
	patch32(34, size);					/* the pixels */
	return 1;
}

/* More than 256 colours: 24 bits a pixel, each row padded to a whole
   number of words, as this always wrote. */
static void save_24(void)
{
	static unsigned char row[(MAXW * 3 + 3) & ~3];
	unsigned char hdr[54];
	unsigned long rowbytes = ((unsigned long)w * 3 + 3) & ~3UL;
	unsigned long datasize = rowbytes * h;
	int r;

	memset(hdr, 0, sizeof hdr);
	hdr[0] = 'B'; hdr[1] = 'M';
	put32(hdr + 2, 54 + datasize);	/* file size */
	put32(hdr + 10, 54);		/* offset to the pixels */
	put32(hdr + 14, 40);		/* BITMAPINFOHEADER */
	put32(hdr + 18, (unsigned long)w);
	put32(hdr + 22, (unsigned long)h);
	put16(hdr + 26, 1);		/* planes */
	put16(hdr + 28, 24);		/* bits per pixel */
	put32(hdr + 34, datasize);
	put32(hdr + 38, 2835);
	put32(hdr + 42, 2835);
	put(hdr, sizeof hdr);
	for (r = y0 + h - 1; r >= y0; r--) {
		need_row(r, 0);
		memset(row, 0, rowbytes);
		row_bgr(r, row);
		put(row, (int)rowbytes);
	}
}

/* ---- PNG ------------------------------------------------------------- */

#define PNG_WINDOW	16384	/* the ring matches are found in */
#define PNG_HASHBITS	12	/* where each of 4096 three-byte hashes was last seen */
#define PNG_IDAT	4096	/* compressed bytes per IDAT chunk */

/* upng.c's: the base values and extra bits of the length codes
   257-285 and of the 30 distance codes */
static const unsigned short LENGTH_BASE[29] = {
	3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51,
	59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const unsigned char LENGTH_EXTRA[29] = {
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4,
	4, 5, 5, 5, 5, 0};
static const unsigned short DISTANCE_BASE[30] = {
	1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385,
	513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385,
	24577};
static const unsigned char DISTANCE_EXTRA[30] = {
	0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10,
	10, 11, 11, 12, 12, 13, 13};

static struct {
	int rowbytes;		/* 3 x the width, without the filter byte */
	unsigned char *prev, *cur;	/* the row above (zeros above the first) and this one, RGB */
	uint32_t pos;		/* the bytes in the zlib stream so far */
	int outlen;		/* the data in the chunk being filled */
	uint32_t bits;		/* the bits not yet written, the first in bit 0 */
	int nbits;
	uint32_t adler1, adler2;	/* the stream's Adler-32, in its two halves */
} png;
static unsigned char png_rows[2][MAXW * 3];
static unsigned char png_win[PNG_WINDOW];	/* the stream's last PNG_WINDOW bytes */
static uint32_t png_head[1 << PNG_HASHBITS];	/* 1 + where a hash was last seen */
static unsigned char png_out[8 + PNG_IDAT];	/* length and type, then the data */
static uint32_t crctab[256];

static void be32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)(v >> 24);
	p[1] = (unsigned char)(v >> 16);
	p[2] = (unsigned char)(v >> 8);
	p[3] = (unsigned char)v;
}

/* the standard CRC-32, PicoMite's lfs_crc */
static uint32_t crc32(uint32_t c, const unsigned char *p, int n)
{
	while (n--)
		c = crctab[(c ^ *p++) & 0xFF] ^ (c >> 8);
	return c;
}

/* A chunk whose len bytes of data are at p + 8: its length, type and
   data, then the CRC of the type and data. */
static void png_chunk(unsigned char *p, const char *type, int len)
{
	unsigned char crc[4];

	be32(p, (uint32_t)len);
	memcpy(p + 4, type, 4);
	be32(crc, crc32(0xFFFFFFFFu, p + 4, len + 4) ^ 0xFFFFFFFFu);
	put(p, len + 8);
	put(crc, 4);
}

static void png_byte(unsigned b)
{
	png_out[8 + png.outlen++] = (unsigned char)b;
	if (png.outlen == PNG_IDAT) {
		png_chunk(png_out, "IDAT", PNG_IDAT);
		png.outlen = 0;
	}
}

/* n bits of v, the lowest first: deflate's order for everything but
   the Huffman codes */
static void png_bits(uint32_t v, int n)
{
	png.bits |= v << png.nbits;
	png.nbits += n;
	while (png.nbits >= 8) {
		png_byte(png.bits & 0xFF);
		png.bits >>= 8;
		png.nbits -= 8;
	}
}

/* an n-bit Huffman code, its top bit first */
static void png_code(uint32_t code, int n)
{
	uint32_t r = 0;
	int i;

	for (i = 0; i < n; i++, code >>= 1)
		r = (r << 1) | (code & 1);
	png_bits(r, n);
}

/* a byte (0-255), the end of the block (256) or a length (257-285), in
   deflate's fixed codes */
static void png_symbol(int s)
{
	if (s < 144)
		png_code(0x30 + s, 8);
	else if (s < 256)
		png_code(0x190 + s - 144, 9);
	else if (s < 280)
		png_code(s - 256, 7);
	else
		png_code(0xC0 + s - 280, 8);
}

/* a copy of len bytes (3-258) from dist bytes back (1-32768) */
static void png_match(unsigned len, unsigned dist)
{
	int i = 28;

	while (LENGTH_BASE[i] > len)
		i--;
	png_symbol(257 + i);
	png_bits(len - LENGTH_BASE[i], LENGTH_EXTRA[i]);
	i = 29;
	while (DISTANCE_BASE[i] > dist)
		i--;
	png_code(i, 5);		/* the fixed distance codes: the number in 5 bits */
	png_bits(dist - DISTANCE_BASE[i], DISTANCE_EXTRA[i]);
}

/* Compress the stream from png.pos to end, which is in the ring. */
static void png_deflate(uint32_t end)
{
	const uint32_t m = PNG_WINDOW - 1;
	unsigned char *wn = png_win;
	uint32_t p = png.pos;

	while (p < end) {
		unsigned len = 0, dist = 0;

		if (end - p >= 3) {
			uint32_t hs = ((uint32_t)(wn[p & m] | wn[(p + 1) & m] << 8
						  | (uint32_t)wn[(p + 2) & m] << 16)
				       * 2654435761u) >> (32 - PNG_HASHBITS);
			uint32_t c = png_head[hs];

			png_head[hs] = p + 1;
			/* a match may run on past p, as deflate copies byte
			   by byte, but not past what is in the ring */
			if (c-- && end - c <= PNG_WINDOW) {
				unsigned max = end - p > 258 ? 258 : end - p;

				while (len < max && wn[(c + len) & m] == wn[(p + len) & m])
					len++;
				dist = p - c;
			}
		}
		if (len >= 3) {
			png_match(len, dist);
			p += len;
		} else {
			png_symbol(wn[p++ & m]);
		}
	}
	png.pos = end;
}

static int paeth(int a, int b, int c)
{
	int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);

	return (pa <= pb && pa <= pc) ? a : pb <= pc ? b : c;
}

/* byte i of this row under filter f: 0 none, 1 sub (the byte to the
   left), 2 up, 3 average, 4 Paeth */
static unsigned char png_filter(int f, int i)
{
	int x = png.cur[i], a = i >= 3 ? png.cur[i - 3] : 0, b = png.prev[i];
	int c = i >= 3 ? png.prev[i - 3] : 0;

	switch (f) {
	case 1: return (unsigned char)(x - a);
	case 2: return (unsigned char)(x - b);
	case 3: return (unsigned char)(x - ((a + b) >> 1));
	case 4: return (unsigned char)(x - paeth(a, b, c));
	}
	return (unsigned char)x;
}

/* The signature, the header (8-bit RGB) and the start of the stream. */
static void png_begin(void)
{
	static const unsigned char sig[8] = {137, 'P', 'N', 'G', '\r', '\n', 26, '\n'};
	unsigned char *hd = png_out + 8;
	uint32_t c;
	int n, k;

	for (n = 0; n < 256; n++) {
		c = (uint32_t)n;
		for (k = 0; k < 8; k++)
			c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
		crctab[n] = c;
	}
	memset(&png, 0, sizeof png);
	png.rowbytes = w * 3;
	png.prev = png_rows[0];
	png.cur = png_rows[1];
	memset(png.prev, 0, png.rowbytes);
	png.adler1 = 1;
	put(sig, 8);
	be32(hd, (uint32_t)w);
	be32(hd + 4, (uint32_t)h);
	hd[8] = 8;			/* bits per sample */
	hd[9] = 2;			/* RGB */
	hd[10] = hd[11] = hd[12] = 0;	/* deflate, the five filters, not interlaced */
	png_chunk(png_out, "IHDR", 13);
	png_byte(0x78);			/* zlib: deflate with a 32K window */
	png_byte(0x01);			/* no dictionary, and the check bits */
	png_bits(1, 1);			/* the last block */
	png_bits(1, 2);			/* of the fixed Huffman codes */
}

/* A row of the picture: blue, green, red, three bytes a pixel. */
static void png_row(const unsigned char *bgr)
{
	const uint32_t m = PNG_WINDOW - 1;
	int n = png.rowbytes, best = 0, i, f;
	uint32_t sum[5] = {0, 0, 0, 0, 0};
	uint32_t p, q, a, b;
	unsigned char *t;

	for (i = 0; i < n; i += 3) {
		png.cur[i] = bgr[i + 2];
		png.cur[i + 1] = bgr[i + 1];
		png.cur[i + 2] = bgr[i];
	}
	for (i = 0; i < n; i++) {
		int x = png.cur[i], pa = i >= 3 ? png.cur[i - 3] : 0, pb = png.prev[i];
		int pc = i >= 3 ? png.prev[i - 3] : 0;

		sum[0] += abs((int8_t)x);
		sum[1] += abs((int8_t)(x - pa));
		sum[2] += abs((int8_t)(x - pb));
		sum[3] += abs((int8_t)(x - ((pa + pb) >> 1)));
		sum[4] += abs((int8_t)(x - paeth(pa, pb, pc)));
	}
	for (f = 1; f < 5; f++)
		if (sum[f] < sum[best])
			best = f;
	p = png.pos;
	png_win[p++ & m] = (unsigned char)best;
	for (i = 0; i < n; i++)
		png_win[p++ & m] = png_filter(best, i);
	/* Adler-32, reduced at least every 5552 bytes, the most that
	   cannot overflow 32 bits */
	a = png.adler1;
	b = png.adler2;
	for (q = png.pos; q < p;) {
		uint32_t k = p - q > 5552 ? q + 5552 : p;

		for (; q < k; q++) {
			a += png_win[q & m];
			b += a;
		}
		a %= 65521;
		b %= 65521;
	}
	png.adler1 = a;
	png.adler2 = b;
	png_deflate(p);
	t = png.prev;
	png.prev = png.cur;
	png.cur = t;
}

/* The end of the block, the Adler-32, the last IDAT and the IEND. */
static void png_end(void)
{
	uint32_t adler = png.adler2 << 16 | png.adler1;

	png_symbol(256);
	if (png.nbits)
		png_bits(0, 8 - png.nbits);
	png_byte(adler >> 24);
	png_byte((adler >> 16) & 0xFF);
	png_byte((adler >> 8) & 0xFF);
	png_byte(adler & 0xFF);
	if (png.outlen)
		png_chunk(png_out, "IDAT", png.outlen);
	png_chunk(png_out, "IEND", 0);
}

static void save_png(void)
{
	static unsigned char row[MAXW * 3];
	int r;

	png_begin();
	for (r = y0; r < y0 + h; r++) {
		need_row(r, 1);
		row_bgr(r, row);
		png_row(row);
	}
	png_end();
	oflush();
}

/* ---- names ----------------------------------------------------------- */

/* MMBasic's AppendDefaultExtension, as loadimage.c's: ext unless the
 * last path component already has an extension.  Without it
 * SAVE IMAGE "pic" : LOAD IMAGE "pic" saves one file and looks for
 * another. */
static const char *defext(const char *name, const char *ext, char *buf, int max)
{
	int n = (int)strlen(name);
	const char *p = name + n;

	while (p > name) {
		p--;
		if (*p == '/' || *p == '\\')
			break;
		if (*p == '.')
			return name;
	}
	if (n + (int)strlen(ext) + 1 > max)
		return name;
	strcpy(buf, name);
	strcat(buf, ext);
	return buf;
}

/* Print Screen's name in dir, ScreenShotSave's (misc/FileIO.c): NULL
   when the first hundred are all taken or the directory cannot be
   looked in. */
static const char *shotname(const char *dir, int ispng, char *buf, int max)
{
	time_t t = time(NULL);
	struct tm *tm = gmtime(&t);
	struct stat st;
	int len, n;

	if (!tm)
		return NULL;
	len = snprintf(buf, max, "%s/SCREEN_%04d%02d%02d_%02d%02d%02d", dir,
		       tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		       tm->tm_hour, tm->tm_min, tm->tm_sec);
	if (len < 0 || len + 8 > max)
		return NULL;
	for (n = 0; n < 100; n++) {
		if (n == 0)
			strcpy(buf + len, ispng ? ".PNG" : ".BMP");
		else
			sprintf(buf + len, ispng ? "_%d.PNG" : "_%d.BMP", n);
		if (stat(buf, &st) < 0)
			return errno == ENOENT ? buf : NULL;
	}
	return NULL;
}

static void usage(const char *me)
{
	fprintf(stderr, "usage: %s [-p] file[.bmp|.png] [x y w h]\n"
			"       %s -s [-p] [dir]\n", me, me);
	exit(1);
}

int main(int argc, char *argv[])
{
	static char namebuf[160];
	const char *me = argv[0];
	int ispng = 0, shot = 0;

	while (argc > 1 && argv[1][0] == '-' && argv[1][1] && !argv[1][2]) {
		if (argv[1][1] == 'p')
			ispng = 1;
		else if (argv[1][1] == 's')
			shot = 1;
		else
			usage(me);
		argc--;
		argv++;
	}
	if (shot ? argc > 2 : (argc != 2 && argc != 6))
		usage(me);

	sysfd = pc3_open_sys();
	if (sysfd < 0) {
		perror("/dev/sys");
		return 1;
	}
	if (pc3_ioctl(sysfd, GFXIOC_INFO, &gi) < 0) {
		perror("GFXIOC_INFO");
		return 1;
	}

	if (argc == 6) {
		x0 = atoi(argv[2]);
		y0 = atoi(argv[3]);
		w  = atoi(argv[4]);
		h  = atoi(argv[5]);
	} else {
		x0 = 0;
		y0 = 0;
		w  = gi.width;
		h  = gi.height;
	}
	/* Clip to the screen rather than refusing: a program asking for
	   more than there is wants what there is. */
	if (x0 < 0) { w += x0; x0 = 0; }
	if (y0 < 0) { h += y0; y0 = 0; }
	if (x0 + w > gi.width)  w = gi.width  - x0;
	if (y0 + h > gi.height) h = gi.height - y0;
	if (w <= 0 || h <= 0 || w > MAXW) {
		fprintf(stderr, "%s: nothing to save\n", me);
		return 1;
	}

	if (shot) {
		oname = shotname(argc == 2 ? argv[1] : ".", ispng,
				 namebuf, (int)sizeof namebuf);
		if (!oname) {
			fprintf(stderr, "%s: no name free for the screen\n", me);
			return 1;
		}
	} else {
		oname = defext(argv[1], ispng ? ".png" : ".bmp",
			       namebuf, (int)sizeof namebuf);
	}
	ofd = open(oname, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (ofd < 0)
		wfail();

	reader_init();
	if (ispng) {
		save_png();
	} else if (!save_rle()) {
		save_24();
		oflush();
	}
	if (close(ofd) < 0)
		wfail();
	return 0;
}

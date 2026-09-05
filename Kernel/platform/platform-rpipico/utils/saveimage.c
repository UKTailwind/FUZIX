/* saveimage - write the screen to a Windows BMP file.
 *
 *   saveimage file.bmp [x y w h]
 *
 * MMBasic's SAVE IMAGE, as a program rather than as firmware.  Nothing
 * about it needs to be inside bcrun or the kernel: it runs once, it is
 * not in anybody's inner loop, and as a separate binary it is
 * cross-compiled with gcc rather than by the compiler on the board.
 * It costs a BASIC program nothing at all - not a byte of the 48K that
 * program has to live in - and it works from the shell on its own.
 *
 * The screen is read a band of rows at a time through GFXIOC_BLITRDR,
 * the rows-out reader: the bytes of the framebuffer as they lie, 4-bit
 * or 1-bit indices, which is what a 320x240 screen costs eight ioctls
 * rather than the 76,800 GETPIXELs this used to make - a tenth of a
 * second on the board, and on a PC, where every ioctl is a round trip
 * to the device server, several seconds.  The indices become colours
 * through GFXIOC_GETPIXEL, once per DISTINCT index at the first pixel
 * that carries it: the kernel resolves the live palette exactly as it
 * did for every pixel before, so the file is the same file, byte for
 * byte, for at most sixteen extra calls.
 *
 * The text console (mode 0xFF) has no rows-out reader - its colours are
 * per cell, not per index - and a kernel older than BLITRDR refuses the
 * first read.  Both fall back to GETPIXEL for every pixel, which is
 * what this always did.
 *
 * BMP is written by hand rather than through a struct: the header is
 * defined in terms of little-endian byte offsets, and a struct would
 * be at the mercy of the compiler's padding.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
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

/* The widest the hardware can be, so one row always fits: 640 pixels
   of three bytes, and BMP rows round up to a multiple of four. */
#define MAXROW	((640 * 3 + 3) & ~3)

/* Rows per BLITRDR: 32 rows of a 320-byte stride is 10K, a fraction of
   what the file's own rows cost, and eight calls for a 240-line mode. */
#define BAND	32

static unsigned char row[MAXROW];
static int sysfd;
static struct gfx_info gi;

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

static int getpixel(int x, int y)
{
	int c = pc3_ioctl(sysfd, GFXIOC_GETPIXEL, (void *)(long)GFX_PACK(x, y));

	return c < 0 ? 0 : c;		/* off-screen reads black */
}

static void emit(unsigned char *p, int c)
{
	p[0] = (unsigned char)c;		/* blue  */
	p[1] = (unsigned char)(c >> 8);		/* green */
	p[2] = (unsigned char)(c >> 16);	/* red   */
}

/* One BMP row from GETPIXEL, the slow way and the only way for the
 * console. */
static void row_by_pixel(int x0, int w, int y)
{
	unsigned char *p = row;
	int x;

	for (x = x0; x < x0 + w; x++, p += 3)
		emit(p, getpixel(x, y));
}

int main(int argc, char *argv[])
{
	unsigned char hdr[54];
	unsigned char *band = NULL;
	unsigned long rowbytes, datasize;
	int fd, x, y, x0, y0, w, h;
	int bpp, bx0 = 0, blen = 0, rowsout = 0;
	long rgb[16];
	unsigned char have[16];

	if (argc != 2 && argc != 6) {
		fprintf(stderr, "usage: %s file.bmp [x y w h]\n", argv[0]);
		return 1;
	}

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
	if (w <= 0 || h <= 0) {
		fprintf(stderr, "%s: nothing to save\n", argv[0]);
		return 1;
	}

	/* The rows-out reader, for the graphics modes: the bytes that hold
	 * pixels x0..x0+w-1 of a row, in the mode's packing - high nibble
	 * the left pixel at 4bpp, bit 7 the left pixel at 1bpp, as
	 * display.c lays them. */
	bpp = gi.bpp;
	if (gi.mode != 0xFF && (bpp == 4 || bpp == 1) && gi.stride) {
		int shift = (bpp == 4) ? 1 : 3;

		bx0 = x0 >> shift;
		blen = ((x0 + w - 1) >> shift) - bx0 + 1;
		band = malloc((size_t)BAND * gi.stride);
		if (band)
			rowsout = 1;
	}
	memset(have, 0, sizeof have);

	rowbytes = ((unsigned long)w * 3 + 3) & ~3UL;
	datasize = rowbytes * h;

	fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0) {
		perror(argv[1]);
		return 1;
	}

	memset(hdr, 0, sizeof(hdr));
	hdr[0] = 'B'; hdr[1] = 'M';
	put32(hdr + 2, 54 + datasize);	/* file size */
	put32(hdr + 10, 54);		/* offset to the pixels */
	put32(hdr + 14, 40);		/* BITMAPINFOHEADER */
	put32(hdr + 18, (unsigned long)w);
	put32(hdr + 22, (unsigned long)h);
	put16(hdr + 26, 1);		/* planes */
	put16(hdr + 28, 24);		/* bits per pixel */
	put32(hdr + 34, datasize);
	put32(hdr + 38, 2835);		/* 72 dpi, in pixels per metre */
	put32(hdr + 42, 2835);
	if (write(fd, hdr, sizeof(hdr)) != (int)sizeof(hdr)) {
		perror(argv[1]);
		return 1;
	}

	/* BMP rows run bottom to top, and each pixel is blue, green, red.
	 * A band is read top-down from the framebuffer and written out
	 * from its last row to its first. */
	for (y = y0 + h - 1; y >= y0; ) {
		int top = y - BAND + 1, r;

		if (top < y0)
			top = y0;
		if (rowsout) {
			struct gfx_blitr gr;

			gr.offset = (uint32_t)top * gi.stride + (uint32_t)bx0;
			gr.len = (uint16_t)blen;
			gr.rows = (uint16_t)(y - top + 1);
			gr.stride = gi.stride;
			gr.pad = 0;
			gr.buf = band;
			if (pc3_ioctl(sysfd, GFXIOC_BLITRDR, &gr) < 0)
				rowsout = 0;	/* an older kernel: the slow way */
		}
		for (r = y; r >= top; r--) {
			memset(row, 0, rowbytes);
			if (!rowsout) {
				row_by_pixel(x0, w, r);
			} else {
				const unsigned char *src = band + (r - top) * blen;
				unsigned char *p = row;

				for (x = x0; x < x0 + w; x++, p += 3) {
					int idx;

					if (bpp == 4) {
						unsigned char v = src[(x >> 1) - bx0];
						idx = (x & 1) ? (v & 15) : (v >> 4);
					} else {
						unsigned char v = src[(x >> 3) - bx0];
						idx = (v >> (7 - (x & 7))) & 1;
					}
					if (!have[idx]) {
						rgb[idx] = getpixel(x, r);
						have[idx] = 1;
					}
					emit(p, (int)rgb[idx]);
				}
			}
			if (write(fd, row, (int)rowbytes) != (int)rowbytes) {
				perror(argv[1]);
				return 1;
			}
		}
		y = top - 1;
	}

	if (close(fd) < 0) {
		perror(argv[1]);
		return 1;
	}
	return 0;
}

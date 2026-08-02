/* loadimage - draw a Windows BMP on the screen.
 *
 *   loadimage file.bmp [x y]
 *
 * MMBasic's LOAD IMAGE, as a program.  The screen has sixteen colours
 * in MODE 2 and two in MODE 1, so anything photographic has to be
 * dithered; the quantiser and the error weights here are MMBasic's
 * own, from BmpDecoder.c and FileIO.c, so a picture lands looking the
 * way it does on the interpreter.
 *
 * Formats: 1, 4, 8, 16, 24 and 32 bits, uncompressed or BI_BITFIELDS,
 * and RLE4 and RLE8.  Bottom-up and top-down.  BITMAPCOREHEADER as
 * well as BITMAPINFOHEADER, and the V4/V5 headers are read as an
 * INFOHEADER with the tail skipped, which is what they are.
 *
 * Rows are fetched by seeking rather than buffered, because a BMP is
 * stored bottom-up and dithering has to run top-down: the error from
 * one row feeds the next.  RLE cannot be seeked into, so it gets the
 * same treatment as the firmware gives it - one sequential pass that
 * records where each row starts, and then the rows are read in the
 * order wanted.
 *
 * Pixels reach the screen through GFXIOC_PIXELS a batch at a time,
 * with a colour for each, so a dithered row is a handful of ioctls
 * rather than one per pixel.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

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

#define MAXW	1024		/* the widest raster the hardware has */
#define MAXH	768
#define BATCH	256

static int sysfd, fd;
static struct gfx_info gi;

static unsigned long  rgbrow[MAXW];	/* one decoded row, RGB888 */
static unsigned char  raw[MAXW * 4];	/* the same row as it lies in the file */
static short  err_r[MAXW + 2], err_g[MAXW + 2], err_b[MAXW + 2];
static short  nxt_r[MAXW + 2], nxt_g[MAXW + 2], nxt_b[MAXW + 2];
static unsigned long  linestart[MAXH];	/* RLE only */
static struct gfx_pt  pts[BATCH];
static unsigned long  cols[BATCH];
static int npts;

static unsigned long pal[256];
static int width, height, bpp, compression, topdown, ncolours;
static unsigned long datastart, rowbytes;
static unsigned long mask_r, mask_g, mask_b;
static int shift_r, shift_g, shift_b, bits_r, bits_g, bits_b;

static void die(const char *what)
{
	fprintf(stderr, "loadimage: %s\n", what);
	exit(1);
}

static unsigned long rd32(const unsigned char *p)
{
	return (unsigned long)p[0] | ((unsigned long)p[1] << 8)
	     | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static unsigned rd16(const unsigned char *p)
{
	return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

/* Where a channel sits in a BITFIELDS pixel, and how wide it is, so it
   can be scaled to a full byte. */
static void maskinfo(unsigned long m, int *shift, int *bits)
{
	int s = 0, n = 0;
	if (m == 0) { *shift = 0; *bits = 0; return; }
	while (!(m & 1)) { m >>= 1; s++; }
	while (m & 1) { m >>= 1; n++; }
	*shift = s;
	*bits = n;
}

static unsigned long chan(unsigned long v, unsigned long m, int s, int n)
{
	unsigned long c;
	if (n == 0)
		return 0;
	c = (v & m) >> s;
	/* Scale n bits up to 8 by replication, which is exact at the ends
	   and is what every decoder does. */
	if (n >= 8)
		return (c >> (n - 8)) & 0xFF;
	c = (c * 255UL) / ((1UL << n) - 1);
	return c & 0xFF;
}

/* ---- the BMP header ------------------------------------------------ */

static void readheader(void)
{
	unsigned char h[64];
	unsigned long hdrsize, off, i;
	int palbytes, entries;

	if (read(fd, h, 14) != 14 || h[0] != 'B' || h[1] != 'M')
		die("not a BMP file");
	off = rd32(h + 10);

	if (read(fd, h, 4) != 4)
		die("truncated header");
	hdrsize = rd32(h);

	if (hdrsize == 12) {			/* BITMAPCOREHEADER */
		if (read(fd, h, 8) != 8)
			die("truncated header");
		width  = (int)rd16(h);
		height = (int)rd16(h + 2);
		bpp    = (int)rd16(h + 6);
		compression = 0;
		ncolours = 0;
		palbytes = 3;
	} else if (hdrsize >= 40) {		/* INFOHEADER, V4, V5 */
		long hh;
		if (read(fd, h, 36) != 36)
			die("truncated header");
		width  = (int)(long)rd32(h);
		hh     = (long)rd32(h + 4);
		if (hh & 0x80000000L)		/* negative: stored top-down */
			hh -= 0x100000000LL;
		height = (int)(hh < 0 ? -hh : hh);
		topdown = (hh < 0);
		bpp    = (int)rd16(h + 10);
		compression = (int)rd32(h + 12);
		ncolours = (int)rd32(h + 28);
		palbytes = 4;
		/* skip whatever the rest of this header version is */
		if (lseek(fd, (long)(14 + hdrsize), 0) < 0)
			die("cannot seek");
	} else {
		die("unknown BMP header");
		return;
	}

	if (width <= 0 || width > MAXW || height <= 0 || height > MAXH)
		die("image too large");

	/* Channel layout.  BITFIELDS says so explicitly; otherwise 16 bit
	   is 5-5-5 and 32 bit is 8-8-8 with the top byte ignored. */
	if (compression == 3) {
		unsigned char m[12];
		if (read(fd, m, 12) != 12)
			die("truncated masks");
		mask_r = rd32(m);
		mask_g = rd32(m + 4);
		mask_b = rd32(m + 8);
	} else if (bpp == 16) {
		mask_r = 0x7C00; mask_g = 0x03E0; mask_b = 0x001F;
	} else {
		mask_r = 0xFF0000; mask_g = 0x00FF00; mask_b = 0x0000FF;
	}
	maskinfo(mask_r, &shift_r, &bits_r);
	maskinfo(mask_g, &shift_g, &bits_g);
	maskinfo(mask_b, &shift_b, &bits_b);

	if (bpp <= 8) {
		entries = ncolours ? ncolours : (1 << bpp);
		if (entries > 256)
			entries = 256;
		if (lseek(fd, (long)(14 + hdrsize), 0) < 0)
			die("cannot seek");
		for (i = 0; i < (unsigned long)entries; i++) {
			unsigned char e[4];
			if (read(fd, e, palbytes) != palbytes)
				die("truncated palette");
			/* stored blue, green, red */
			pal[i] = ((unsigned long)e[2] << 16)
			       | ((unsigned long)e[1] << 8) | e[0];
		}
	}

	datastart = off;
	rowbytes = (((unsigned long)width * bpp + 31) / 32) * 4;
}

/* ---- getting one row of the image as RGB888 ------------------------- */

static void expandrow(const unsigned char *p)
{
	int x;

	switch (bpp) {
	case 1:
		for (x = 0; x < width; x++)
			rgbrow[x] = pal[(p[x >> 3] >> (7 - (x & 7))) & 1];
		break;
	case 4:
		for (x = 0; x < width; x++)
			rgbrow[x] = pal[(x & 1) ? (p[x >> 1] & 15)
					        : (p[x >> 1] >> 4)];
		break;
	case 8:
		for (x = 0; x < width; x++)
			rgbrow[x] = pal[p[x]];
		break;
	case 16:
		for (x = 0; x < width; x++) {
			unsigned long v = rd16(p + x * 2);
			rgbrow[x] = (chan(v, mask_r, shift_r, bits_r) << 16)
				  | (chan(v, mask_g, shift_g, bits_g) << 8)
				  |  chan(v, mask_b, shift_b, bits_b);
		}
		break;
	case 24:
		for (x = 0; x < width; x++)
			rgbrow[x] = ((unsigned long)p[x * 3 + 2] << 16)
				  | ((unsigned long)p[x * 3 + 1] << 8)
				  |  p[x * 3];
		break;
	case 32:
		for (x = 0; x < width; x++) {
			unsigned long v = rd32(p + x * 4);
			rgbrow[x] = (chan(v, mask_r, shift_r, bits_r) << 16)
				  | (chan(v, mask_g, shift_g, bits_g) << 8)
				  |  chan(v, mask_b, shift_b, bits_b);
		}
		break;
	default:
		die("unsupported colour depth");
	}
}

/* One RLE row, decoded into rgbrow.  Returns 0 at the end of the data. */
static int rlerow(void)
{
	int x = 0;
	unsigned char c[2];

	for (x = 0; x < width; x++)
		rgbrow[x] = pal[0];
	x = 0;
	for (;;) {
		if (read(fd, c, 2) != 2)
			return 0;
		if (c[0]) {			/* a run of c[0] pixels */
			int n = c[0], i;
			for (i = 0; i < n && x < width; i++, x++) {
				int idx = (bpp == 8) ? c[1]
					: ((i & 1) ? (c[1] & 15) : (c[1] >> 4));
				rgbrow[x] = pal[idx];
			}
		} else if (c[1] == 0) {		/* end of line */
			return 1;
		} else if (c[1] == 1) {		/* end of the bitmap */
			return 0;
		} else if (c[1] == 2) {		/* a delta, which we cannot
						   honour without moving down */
			if (read(fd, c, 2) != 2)
				return 0;
			x += c[0];
			if (x > width)
				x = width;
		} else {			/* absolute mode */
			int n = c[1], i;
			int bytes = (bpp == 8) ? n : ((n + 1) / 2);
			int pad = (bytes & 1);
			if (read(fd, raw, bytes) != bytes)
				return 0;
			for (i = 0; i < n && x < width; i++, x++) {
				int idx = (bpp == 8) ? raw[i]
					: ((i & 1) ? (raw[i >> 1] & 15)
						   : (raw[i >> 1] >> 4));
				rgbrow[x] = pal[idx];
			}
			if (pad && read(fd, c, 1) != 1)
				return 0;
		}
	}
}

/* RLE cannot be seeked into, so walk it once and note where each row
   begins - the firmware's line start table, for the same reason. */
static int buildlinetable(void)
{
	int y = 0;
	if (lseek(fd, (long)datastart, 0) < 0)
		die("cannot seek");
	while (y < height) {
		long here = lseek(fd, 0, 1);
		if (here < 0)
			die("cannot seek");
		linestart[y] = (unsigned long)here;
		if (!rlerow())
			break;
		y++;
	}
	return y;
}

static void fetchrow(int imgy, int rlerows)
{
	if (compression == 1 || compression == 2) {
		if (imgy >= rlerows) {
			int x;
			for (x = 0; x < width; x++)
				rgbrow[x] = pal[0];
			return;
		}
		if (lseek(fd, (long)linestart[imgy], 0) < 0)
			die("cannot seek");
		rlerow();
		return;
	}
	if (lseek(fd, (long)(datastart + (unsigned long)imgy * rowbytes), 0) < 0)
		die("cannot seek");
	if (read(fd, raw, (int)rowbytes) != (int)rowbytes)
		die("truncated image data");
	expandrow(raw);
}

/* ---- dithering and drawing ------------------------------------------ */

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

/*
 * The display's own colours.  MODE 2 is RGB121 - one bit of red, two
 * of green, one of blue - and MODE 1 has ink and paper only.  The
 * quantiser is the interpreter's rgb888_to_rgb121_dither: a threshold
 * on red and blue, and green rounded to the nearest quarter.
 */
static unsigned long quantise(int r, int g, int b, int *qr, int *qg, int *qb)
{
	if (gi.bpp == 1) {
		/* luminance, the usual weights */
		int y = (r * 77 + g * 151 + b * 28) >> 8;
		int on = (y >= 128);
		*qr = *qg = *qb = on ? 255 : 0;
		return on ? 0xFFFFFFUL : 0UL;
	} else {
		int r1 = (r >= 128) ? 1 : 0;
		int g2 = (g * 3 + 127) / 255;
		int b1 = (b >= 128) ? 1 : 0;
		*qr = r1 * 255;
		*qg = g2 * 85;
		*qb = b1 * 255;
		/* The palette's own value for this cell, so the kernel maps
		   it back to exactly the entry that was chosen. */
		return ((unsigned long)(r1 * 255) << 16)
		     | ((unsigned long)(g2 == 0 ? 0 : g2 == 1 ? 0x40
					: g2 == 2 ? 0x80 : 0xFF) << 8)
		     |  (unsigned long)(b1 * 255);
	}
}

static void ditherrow(int screeny, int x0, int w)
{
	int x;

	for (x = 0; x < w + 2; x++)
		nxt_r[x] = nxt_g[x] = nxt_b[x] = 0;

	for (x = 0; x < w; x++) {
		unsigned long p = rgbrow[x];
		int r = (int)((p >> 16) & 0xFF) + err_r[x + 1];
		int g = (int)((p >> 8) & 0xFF)  + err_g[x + 1];
		int b = (int)(p & 0xFF)         + err_b[x + 1];
		int qr, qg, qb, er, eg, eb;
		unsigned long out;

		if (r < 0) r = 0; else if (r > 255) r = 255;
		if (g < 0) g = 0; else if (g > 255) g = 255;
		if (b < 0) b = 0; else if (b > 255) b = 255;

		out = quantise(r, g, b, &qr, &qg, &qb);
		er = r - qr;
		eg = g - qg;
		eb = b - qb;

		/* Floyd-Steinberg:      X   7/16
		                    3/16 5/16 1/16   */
		err_r[x + 2] += (short)((er * 7) / 16);
		err_g[x + 2] += (short)((eg * 7) / 16);
		err_b[x + 2] += (short)((eb * 7) / 16);
		nxt_r[x]     += (short)((er * 3) / 16);
		nxt_g[x]     += (short)((eg * 3) / 16);
		nxt_b[x]     += (short)((eb * 3) / 16);
		nxt_r[x + 1] += (short)((er * 5) / 16);
		nxt_g[x + 1] += (short)((eg * 5) / 16);
		nxt_b[x + 1] += (short)((eb * 5) / 16);
		nxt_r[x + 2] += (short)(er / 16);
		nxt_g[x + 2] += (short)(eg / 16);
		nxt_b[x + 2] += (short)(eb / 16);

		if (x0 + x >= 0 && x0 + x < gi.width)
			plot(x0 + x, screeny, out);
	}
	flush();
	memcpy(err_r, nxt_r, sizeof(err_r));
	memcpy(err_g, nxt_g, sizeof(err_g));
	memcpy(err_b, nxt_b, sizeof(err_b));
}

int main(int argc, char *argv[])
{
	int x0 = 0, y0 = 0, y, rlerows = 0;

	if (argc != 2 && argc != 4) {
		fprintf(stderr, "usage: %s file.bmp [x y]\n", argv[0]);
		return 1;
	}
	if (argc == 4) {
		x0 = atoi(argv[2]);
		y0 = atoi(argv[3]);
	}

	sysfd = open("/dev/sys", O_RDWR);
	if (sysfd < 0)
		die("no /dev/sys");
	if (ioctl(sysfd, GFXIOC_INFO, &gi) < 0)
		die("cannot ask about the screen");

	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		perror(argv[1]);
		return 1;
	}
	readheader();
	if (compression == 1 || compression == 2) {
		if (bpp != 8 && bpp != 4)
			die("RLE needs 4 or 8 bits");
		rlerows = buildlinetable();
	}

	memset(err_r, 0, sizeof(err_r));
	memset(err_g, 0, sizeof(err_g));
	memset(err_b, 0, sizeof(err_b));

	/* Top down on the screen whichever way the file is stored, because
	   the error diffusion only makes sense in that order. */
	for (y = 0; y < height; y++) {
		int screeny = y0 + y;
		fetchrow(topdown ? y : (height - 1 - y), rlerows);
		if (screeny < 0 || screeny >= gi.height)
			continue;	/* still dithered, so the rows below
					   inherit the right error */
		ditherrow(screeny, x0, width);
	}

	close(fd);
	return 0;
}

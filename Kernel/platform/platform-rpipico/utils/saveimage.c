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
 * The screen is read through GFXIOC_GETPIXEL, one pixel at a time.
 * That is slow - a full 320x240 screen is 76,800 ioctls, about a tenth
 * of a second - but it is the only reader the kernel has today, it
 * needs no buffer anywhere, and it returns RGB888 in every mode
 * including the text console, where it resolves each cell's own
 * foreground and background.  A rectangle reader would make this
 * roughly a hundred times faster and let the file be 4-bit with a
 * palette; the file format below is the only thing that would change.
 *
 * BMP is written by hand rather than through a struct: the header is
 * defined in terms of little-endian byte offsets, and a struct would
 * be at the mercy of the compiler's padding.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define GFXIOC_INFO	0x000E
#define GFXIOC_GETPIXEL	0x0011
#define GFX_PACK(x, y)	(((x) & 0x3FF) | (((y) & 0x1FF) << 10))

struct gfx_info {
	unsigned short width, height, stride;
	unsigned char bpp, mode;
};

/* The widest the hardware can be, so one row always fits: 640 pixels
   of three bytes, and BMP rows round up to a multiple of four. */
#define MAXROW	((640 * 3 + 3) & ~3)

static unsigned char row[MAXROW];

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

int main(int argc, char *argv[])
{
	struct gfx_info gi;
	unsigned char hdr[54];
	unsigned long rowbytes, datasize;
	int sysfd, fd, x, y, x0, y0, w, h;

	if (argc != 2 && argc != 6) {
		fprintf(stderr, "usage: %s file.bmp [x y w h]\n", argv[0]);
		return 1;
	}

	sysfd = open("/dev/sys", O_RDWR);
	if (sysfd < 0) {
		perror("/dev/sys");
		return 1;
	}
	if (ioctl(sysfd, GFXIOC_INFO, &gi) < 0) {
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

	/* BMP rows run bottom to top, and each pixel is blue, green, red. */
	for (y = y0 + h - 1; y >= y0; y--) {
		unsigned char *p = row;
		memset(row, 0, rowbytes);
		for (x = x0; x < x0 + w; x++) {
			int c = ioctl(sysfd, GFXIOC_GETPIXEL,
				      (void *)(long)GFX_PACK(x, y));
			if (c < 0)
				c = 0;		/* off-screen reads black */
			*p++ = (unsigned char)c;		/* blue  */
			*p++ = (unsigned char)(c >> 8);		/* green */
			*p++ = (unsigned char)(c >> 16);	/* red   */
		}
		if (write(fd, row, (int)rowbytes) != (int)rowbytes) {
			perror(argv[1]);
			return 1;
		}
	}

	if (close(fd) < 0) {
		perror(argv[1]);
		return 1;
	}
	return 0;
}

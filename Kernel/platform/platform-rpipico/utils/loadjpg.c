/* loadjpg - draw a JPEG on the screen.
 *
 *   loadjpg file.jpg [x [y [mode [ximage [yimage [scale]]]]]]
 *
 * MMBasic's LOAD JPG, as a program, and the same shape as loadimage:
 * the interpreter hands /usr/bin/loadjpg an argv and waits.  The
 * decoder is MMBasic's own picojpeg (third_party_mod/picojpeg.c),
 * unmodified, so a file that decodes on a PicoMite decodes here.
 *
 * WHY A PROGRAM AND NOT THE RUNTIME.  Every byte compiled into
 * mmb_*.h is a byte in every BASIC program that includes it, and the
 * decoder is 60K of source.  As a program it costs a resident BASIC
 * program nothing at all, and it is loaded only while the picture is
 * being drawn - which is also why it must be frugal: it runs WHILE a
 * BASIC program is resident in an 84-block pool.
 *
 * picojpeg decodes an MCU at a time rather than an image at a time -
 * that is the whole reason it is the right decoder here.  Nothing
 * here ever holds the whole picture, and unlike the reference it does
 * not hold an MCU ROW either: MMBasic buffers one so it can dither
 * across it, and we do not dither, so each MCU is drawn as it is
 * decoded.  That is about 30K saved on a 640-wide photo and it makes
 * the footprint independent of the picture's width.  (upng, which
 * LOAD PNG needs, cannot work this way - see loadpng.c.)
 *
 * NO DITHERING, for loadimage.c's reason: error diffusion wants two
 * rows of signed error per channel, which is thousands of bytes of a
 * pool a BASIC program is already sitting in.  The mode argument is
 * still parsed, because it is positional and the image offsets come
 * after it, but nothing acts on it.  What is left is the quantiser,
 * which is the part that decides the colour, and it is the same one
 * loadimage uses - MMBasic's rgb888_to_rgb121_dither - so a colour
 * lands on the palette entry the interpreter would pick.
 *
 * Pixels reach the screen through GFXIOC_PIXELS a batch at a time, so
 * a row is a handful of ioctls rather than one per pixel.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "picojpeg.h"

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

static int sysfd, fd;
static struct gfx_info gi;

static struct gfx_pt  pts[BATCH];
static unsigned long  cols[BATCH];
static int npts;

/*
 * picojpeg's working buffers.  MMBasic's copy declares these extern so
 * the caller places them (it uses the interpreter's temp memory); we
 * allocate them once here.  Together they are about 1.4K - the whole
 * point of this decoder.
 */
int16_t *gCoeffBuf;
uint8_t *gMCUBufR, *gMCUBufG, *gMCUBufB;
int16_t *gQuant0, *gQuant1;
uint8_t *gHuffVal2, *gHuffVal3;
uint8_t *gInBuf;

static unsigned long filesize, filepos;

static void die(const char *what)
{
	fprintf(stderr, "loadjpg: %s\n", what);
	exit(1);
}

static void *xalloc(unsigned long n)
{
	void *p = malloc((size_t)n);

	if (p == NULL)
		die("out of memory - image too large");
	return p;
}

/* ---- input ---------------------------------------------------------- */

/*
 * picojpeg pulls its input through this rather than reading a file
 * itself, which is what lets it work from a stream.  Short reads at the
 * end are normal: the decoder asks for a full buffer and takes what it
 * gets.
 */
static unsigned char need_bytes(unsigned char *pBuf, unsigned char buf_size,
				unsigned char *pBytes_actually_read,
				void *pCallback_data)
{
	unsigned long n = filesize - filepos;
	int got;

	(void)pCallback_data;
	if (n > (unsigned long)buf_size)
		n = buf_size;
	got = read(fd, pBuf, (int)n);
	if (got < 0)
		return PJPG_STREAM_READ_ERROR;
	filepos += (unsigned long)got;
	*pBytes_actually_read = (unsigned char)got;
	return 0;
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

/*
 * The display's own colours - loadimage.c's quantiser, unchanged.
 * MODE 2 is RGB121, MODE 1 is ink and paper on luminance.
 */
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
 * Where a pixel of the just-decoded MCU lives.
 *
 * The MCU buffer is a row of 8x8 BLOCKS, not a raster: the block at
 * (bx*8, by*8) starts at bx*64 + by*128, and within a block the row
 * stride is 8.  Getting this wrong reads a neighbouring block and the
 * picture comes out in tiles, which is the tell.
 */
static unsigned mcuofs(const pjpeg_image_info_t *info, int px, int py)
{
	unsigned bx = (unsigned)(px >> 3), by = (unsigned)(py >> 3);

	(void)info;
	return bx * 64u + by * 128u
	     + (unsigned)(py & 7) * 8u + (unsigned)(px & 7);
}

/* ---- main ----------------------------------------------------------- */

int main(int argc, char **argv)
{
	pjpeg_image_info_t info;
	unsigned char status;
	int xorg = 0, yorg = 0, xoff = 0, yoff = 0, scale = 1;
	int mcu_x, mcu_y;

	if (argc < 2) {
		fprintf(stderr, "usage: loadjpg file.jpg "
				"[x [y [mode [ximage [yimage [scale]]]]]]\n");
		return 1;
	}
	if (argc > 2) xorg  = atoi(argv[2]);
	if (argc > 3) yorg  = atoi(argv[3]);
	/* argv[4] is the dither mode: parsed, not acted on - see above. */
	if (argc > 5) xoff  = atoi(argv[5]);
	if (argc > 6) yoff  = atoi(argv[6]);
	if (argc > 7) scale = atoi(argv[7]);
	if (scale != 1 && scale != 2 && scale != 4 && scale != 8)
		die("scale must be 1, 2, 4 or 8");
	if (xoff < 0 || yoff < 0)
		die("image offset must not be negative");

	sysfd = open("/dev/sys", O_RDWR);
	if (sysfd < 0)
		die("cannot open /dev/sys");
	if (ioctl(sysfd, GFXIOC_INFO, &gi) < 0)
		die("no display");

	fd = open(argv[1], O_RDONLY);
	if (fd < 0)
		die("cannot open the file");
	filesize = (unsigned long)lseek(fd, 0L, 2);
	if (lseek(fd, 0L, 0) < 0)
		die("cannot seek");
	filepos = 0;

	gCoeffBuf = (int16_t *)xalloc(8 * 8 * sizeof(int16_t));
	gMCUBufR  = (uint8_t *)xalloc(256);
	gMCUBufG  = (uint8_t *)xalloc(256);
	gMCUBufB  = (uint8_t *)xalloc(256);
	gQuant0   = (int16_t *)xalloc(8 * 8 * sizeof(int16_t));
	gQuant1   = (int16_t *)xalloc(8 * 8 * sizeof(int16_t));
	gHuffVal2 = (uint8_t *)xalloc(256);
	gHuffVal3 = (uint8_t *)xalloc(256);
	gInBuf    = (uint8_t *)xalloc(PJPG_MAX_IN_BUF_SIZE);

	status = pjpeg_decode_init(&info, need_bytes, NULL, 0);
	if (status) {
		if (status == PJPG_UNSUPPORTED_MODE)
			die("progressive JPEG files are not supported");
		fprintf(stderr, "loadjpg: decode_init failed, status %d\n",
			(int)status);
		return 1;
	}
	if (xoff >= info.m_width || yoff >= info.m_height)
		die("image offset is outside the picture");

	for (mcu_y = 0; mcu_y < info.m_MCUSPerCol; mcu_y++) {
		int image_y = mcu_y * info.m_MCUHeight;
		int limit = yoff + scale * (gi.height - yorg);

		if (image_y >= limit)
			break;

		for (mcu_x = 0; mcu_x < info.m_MCUSPerRow; mcu_x++) {
			int image_x = mcu_x * info.m_MCUWidth;
			int px, py;

			/*
			 * Every MCU is decoded whether it is drawn or
			 * not: the stream is sequential and there is no
			 * seeking into the middle of it.  Only the
			 * drawing is skipped.
			 */
			status = pjpeg_decode_mcu();
			if (status) {
				if (status != PJPG_NO_MORE_BLOCKS) {
					fprintf(stderr, "loadjpg: decode_mcu "
						"failed, status %d\n",
						(int)status);
					return 1;
				}
				flush();
				close(fd);
				return 0;
			}

			/*
			 * Bin `scale` by `scale` source pixels into one
			 * screen pixel.  A bin never straddles an MCU:
			 * MCU sides are 8 or 16 and scale is at most 8,
			 * so the samples a bin wants are all in the MCU
			 * that has just been decoded - which is what lets
			 * the row buffer go.
			 */
			for (py = 0; py < info.m_MCUHeight; py += scale) {
				int sy = image_y + py;
				int screeny;

				if (sy < yoff || sy >= info.m_height)
					continue;
				if ((sy - yoff) % scale)
					continue;
				screeny = yorg + (sy - yoff) / scale;
				if (screeny < 0 || screeny >= gi.height)
					continue;

				for (px = 0; px < info.m_MCUWidth; px += scale) {
					int sx = image_x + px;
					int screenx;
					unsigned long r = 0, g = 0, b = 0;
					int n = 0, i, j;

					if (sx < xoff || sx >= info.m_width)
						continue;
					if ((sx - xoff) % scale)
						continue;
					screenx = xorg + (sx - xoff) / scale;
					if (screenx < 0)
						continue;
					if (screenx >= gi.width)
						continue;

					for (j = 0; j < scale; j++) {
						if (py + j >= info.m_MCUHeight
						    || sy + j >= info.m_height)
							break;
						for (i = 0; i < scale; i++) {
							unsigned o;

							if (px + i >= info.m_MCUWidth
							    || sx + i >= info.m_width)
								break;
							o = mcuofs(&info,
								   px + i,
								   py + j);
							r += info.m_pMCUBufR[o];
							g += info.m_pMCUBufG[o];
							b += info.m_pMCUBufB[o];
							n++;
						}
					}
					if (n == 0)
						continue;
					plot(screenx, screeny,
					     tocolour((int)(r / n),
						      (int)(g / n),
						      (int)(b / n)));
				}
			}
			flush();
		}
	}
	flush();
	close(fd);
	return 0;
}

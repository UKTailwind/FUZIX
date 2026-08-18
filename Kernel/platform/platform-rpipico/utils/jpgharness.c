/* jpgharness - run loadjpg's own code on the host and write a PPM.
 *
 *	cc -o jpgharness jpgharness.c picojpeg.c && ./jpgharness pic.jpg out.ppm
 *
 * loadjpg cannot be run here - it is an ARM binary that talks to
 * /dev/sys - so this compiles ITS SOURCE with the two system calls it
 * makes intercepted: the /dev/sys open returns a fake descriptor, and
 * GFXIOC_PIXELS paints into an array instead of the screen.  Everything
 * between - picojpeg, the MCU addressing, the binning, the quantiser -
 * is the shipped code, unmodified.
 *
 * That matters because the MCU block addressing is the one thing here
 * that is easy to get subtly wrong and impossible to see from a
 * compile: an off-by-one block stride reads a neighbour and the picture
 * comes out in 8x8 tiles.  A PPM you can look at settles it before the
 * board is involved.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>

#define HARNESS_W 320
#define HARNESS_H 240

static unsigned long fb[HARNESS_H][HARNESS_W];
static int painted;

/* ---- the two calls loadjpg makes on the system ---------------------- */

#define GFXIOC_INFO	0x000E
#define GFXIOC_PIXELS	0x0014

struct h_info { unsigned short width, height, stride; unsigned char bpp, mode; };
struct h_pt { short x, y; };
struct h_batch { unsigned short count, flags; void *items, *colours; };

static int h_ioctl(int fd, unsigned long req, ...)
{
	va_list ap;
	void *arg;

	(void)fd;
	va_start(ap, req);
	arg = va_arg(ap, void *);
	va_end(ap);
	if (req == GFXIOC_INFO) {
		struct h_info *g = arg;

		g->width = HARNESS_W;
		g->height = HARNESS_H;
		g->stride = HARNESS_W / 2;
		g->bpp = 4;
		g->mode = 7;
		return 0;
	}
	if (req == GFXIOC_PIXELS) {
		struct h_batch *b = arg;
		struct h_pt *p = b->items;
		unsigned long *c = b->colours;
		int i;

		for (i = 0; i < b->count; i++) {
			if (p[i].x < 0 || p[i].x >= HARNESS_W ||
			    p[i].y < 0 || p[i].y >= HARNESS_H) {
				fprintf(stderr, "jpgharness: pixel off screen "
					"at %d,%d\n", p[i].x, p[i].y);
				exit(2);
			}
			fb[p[i].y][p[i].x] = c[i];
			painted++;
		}
		return 0;
	}
	return -1;
}

static int h_open(const char *path, int flags, ...)
{
	if (!strcmp(path, "/dev/sys"))
		return 999;			/* the fake display */
	return open(path, flags);
}

/* Pull in the shipped source with those two redirected. */
#define ioctl	h_ioctl
#define open	h_open
#define main	loadjpg_main
#include "loadjpg.c"
#undef main
#undef open
#undef ioctl

int main(int argc, char **argv)
{
	char *av[8];
	int rc, x, y;
	FILE *o;

	if (argc < 3) {
		fprintf(stderr, "usage: jpgharness pic.jpg out.ppm "
				"[x y mode xi yi scale]\n");
		return 1;
	}
	memset(fb, 0, sizeof(fb));
	av[0] = "loadjpg";
	av[1] = argv[1];
	av[2] = argc > 3 ? argv[3] : NULL;
	av[3] = argc > 4 ? argv[4] : NULL;
	av[4] = argc > 5 ? argv[5] : NULL;
	av[5] = argc > 6 ? argv[6] : NULL;
	av[6] = argc > 7 ? argv[7] : NULL;
	av[7] = argc > 8 ? argv[8] : NULL;
	{
		int n = 2;

		while (n < 8 && av[n] != NULL)
			n++;
		rc = loadjpg_main(n, av);
	}
	if (rc != 0) {
		fprintf(stderr, "jpgharness: loadjpg returned %d\n", rc);
		return rc;
	}

	o = fopen(argv[2], "wb");
	if (o == NULL) {
		perror(argv[2]);
		return 1;
	}
	fprintf(o, "P6\n%d %d\n255\n", HARNESS_W, HARNESS_H);
	for (y = 0; y < HARNESS_H; y++)
		for (x = 0; x < HARNESS_W; x++) {
			unsigned long c = fb[y][x];
			unsigned char px[3];

			px[0] = (unsigned char)((c >> 16) & 0xFF);
			px[1] = (unsigned char)((c >> 8) & 0xFF);
			px[2] = (unsigned char)(c & 0xFF);
			fwrite(px, 1, 3, o);
		}
	fclose(o);
	printf("jpgharness: %d pixels painted -> %s\n", painted, argv[2]);
	return 0;
}

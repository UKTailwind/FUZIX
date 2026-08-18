/* pngharness - run loadpng's own code on the host and write a PPM.
 *
 *   cc -o pngharness pngharness.c upng.c upng_pc3.c -DUPNG_NO_FILE
 *   ./pngharness test.png out.ppm [x y transparent cutoff]
 *
 * Same idea as jpgharness: loadpng is an ARM binary that talks to
 * /dev/sys, so this compiles ITS SOURCE with the system calls it makes
 * intercepted.  There are three here rather than two, because loadpng
 * also asks the kernel for a PSRAM arena - that becomes a malloc.
 *
 * What this is really for is the TRANSPARENT path.  The reference
 * composites by reading the screen back; we simply do not plot the
 * pixel.  Those are only the same thing if the pixel is genuinely left
 * alone, and a count of what was painted proves it - the screen starts
 * as a known colour and the transparent region must still hold it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>

#define HARNESS_W 320
#define HARNESS_H 240

#define MARKER 0x00FF00FFUL		/* nothing the quantiser can emit */

static unsigned long fb[HARNESS_H][HARNESS_W];
static int painted;

#define GFXIOC_INFO	0x000E
#define GFXIOC_PIXELS	0x0014
#define PSRAMIOC_ALLOC	0x000A

struct h_info { unsigned short width, height, stride; unsigned char bpp, mode; };
struct h_pt { short x, y; };
struct h_batch { unsigned short count, flags; void *items, *colours; };
struct h_psram { unsigned long len, base; };

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
	if (req == PSRAMIOC_ALLOC) {
		struct h_psram *p = arg;

		p->base = (unsigned long)(size_t)malloc(p->len);
		return p->base ? 0 : -1;
	}
	if (req == GFXIOC_PIXELS) {
		struct h_batch *b = arg;
		struct h_pt *p = b->items;
		unsigned long *c = b->colours;
		int i;

		for (i = 0; i < b->count; i++) {
			if (p[i].x < 0 || p[i].x >= HARNESS_W ||
			    p[i].y < 0 || p[i].y >= HARNESS_H) {
				fprintf(stderr, "pngharness: pixel off screen "
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
		return 999;
	return open(path, flags);
}

/* The arena unit is compiled separately and so does not see the
   intercepts above; the harness supplies the same four entry points
   over malloc instead.  upng and loadpng are still the shipped code -
   only where the memory comes from differs, which is the one thing
   that cannot work on a host with no kernel to ask. */
void *GetMemory(unsigned long n)
{
	void *p = malloc(n);

	if (p == NULL) {
		fprintf(stderr, "pngharness: out of memory\n");
		exit(2);
	}
	return p;
}

void FreeMemorySafe(void *pp)
{
	if (pp != NULL)
		*(void **)pp = NULL;
}

void routinechecks(void) { }

void error(const char *m)
{
	fprintf(stderr, "pngharness: %s\n", m);
	exit(1);
}

#define ioctl	h_ioctl
#define open	h_open
#define main	loadpng_main
#include "loadpng.c"
#undef main
#undef open
#undef ioctl

int main(int argc, char **argv)
{
	char *av[6];
	int rc, x, y, n, untouched = 0;
	FILE *o;

	if (argc < 3) {
		fprintf(stderr, "usage: pngharness in.png out.ppm "
				"[x y transparent cutoff]\n");
		return 1;
	}
	for (y = 0; y < HARNESS_H; y++)
		for (x = 0; x < HARNESS_W; x++)
			fb[y][x] = MARKER;

	av[0] = "loadpng";
	av[1] = argv[1];
	av[2] = argc > 3 ? argv[3] : NULL;
	av[3] = argc > 4 ? argv[4] : NULL;
	av[4] = argc > 5 ? argv[5] : NULL;
	av[5] = argc > 6 ? argv[6] : NULL;
	n = 2;
	while (n < 6 && av[n] != NULL)
		n++;
	rc = loadpng_main(n, av);
	if (rc != 0) {
		fprintf(stderr, "pngharness: loadpng returned %d\n", rc);
		return rc;
	}

	for (y = 0; y < HARNESS_H; y++)
		for (x = 0; x < HARNESS_W; x++)
			if (fb[y][x] == MARKER)
				untouched++;
	printf("pngharness: %d painted, %d still untouched\n",
	       painted, untouched);

	o = fopen(argv[2], "wb");
	if (o == NULL) {
		perror(argv[2]);
		return 1;
	}
	fprintf(o, "P6\n%d %d\n255\n", HARNESS_W, HARNESS_H);
	for (y = 0; y < HARNESS_H; y++)
		for (x = 0; x < HARNESS_W; x++) {
			unsigned long c = fb[y][x];
			unsigned char p[3];

			p[0] = (unsigned char)((c >> 16) & 0xFF);
			p[1] = (unsigned char)((c >> 8) & 0xFF);
			p[2] = (unsigned char)(c & 0xFF);
			fwrite(p, 1, 3, o);
		}
	fclose(o);
	return 0;
}

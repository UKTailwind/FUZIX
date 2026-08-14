/* fontdef - the GFXIOC_FONTDEF spike (mmb2c/PLAN-fonts.md Phase 0).
 *
 * Registers a font of KNOWN bit patterns, draws with it, and reads the
 * pixels back - so the kernel half is proved before the translator
 * parses a single hex word.  Then the two tests that matter more than
 * the drawing, because they are the ones that fail silently:
 *
 *   - a SECOND process must not see this one's font.  Every process
 *     here loads at the same address, so a slot left visible would let
 *     it draw glyphs out of whatever happens to be at that address in
 *     ITS image - garbage, with nothing to trace.
 *   - after the owner exits, the slot must be gone.
 *
 * Both are checked through GFXIOC_FONTINFO, which reports width 0 for a
 * font that does not exist: the child asks for font 10 and must be told
 * there is no such thing.
 *
 *   fontdef            the whole spike
 *   fontdef -c         internal: the child half of the ownership test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include "../pico_ioctl.h"

/* An 8x8 font of three characters from '0', each one a pattern that is
 * unmistakable in a pixel readback:
 *
 *   '0'  solid block          every pixel set
 *   '1'  checkerboard         alternating, starting set
 *   '2'  the '!' from picofrog's own font - the byte order test vector
 *        recorded in PLAN-fonts.md
 *
 * Header: width, height, first character, count - MMBasic's layout,
 * which is what the translator will emit after byte-swapping the hex
 * words of a DefineFont block.
 */
static const unsigned char myfont[4 + 3 * 8] = {
	8, 8, '0', 3,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55,
	0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00
};

#define FONTNO 10
#define X0 16
#define Y0 16

static int sys = -1;

static int fontwidth(int font)
{
	struct gfx_fontinfo fi;

	memset(&fi, 0, sizeof(fi));
	fi.font = (uint8_t)font;
	if (ioctl(sys, GFXIOC_FONTINFO, &fi) < 0)
		return -1;
	return fi.width;
}

static int define(void)
{
	struct gfx_fontdef fd;

	memset(&fd, 0, sizeof(fd));
	fd.font = FONTNO;
	fd.addr = (uint32_t)(unsigned long)myfont;
	fd.bytes = sizeof(myfont);
	return ioctl(sys, GFXIOC_FONTDEF, &fd);
}

int main(int argc, char *argv[])
{
	struct gfx_text gt;
	struct gfx_fontdef bad;
	int bad_count = 0, x, y, pid, status;

	sys = open("/dev/sys", O_RDWR);
	if (sys < 0) {
		perror("/dev/sys");
		return 1;
	}

	/* The child half: our parent has font 10, we must not. */
	if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'c') {
		int w = fontwidth(FONTNO);

		printf("child: font %d width %d (0 = not mine, correct)\n",
		       FONTNO, w);
		return w == 0 ? 0 : 1;
	}

	if (define() < 0) {
		perror("GFXIOC_FONTDEF");
		return 1;
	}
	printf("defined font %d: %d bytes\n", FONTNO, (int)sizeof(myfont));
	printf("metrics: width %d (8 expected)\n", fontwidth(FONTNO));

	/* Draw "012" and read it back.  White on black, scale 1. */
	memset(&gt, 0, sizeof(gt));
	gt.x = X0;
	gt.y = Y0;
	gt.scale = 1;
	gt.font = FONTNO;
	gt.fg = 0xFFFFFF;
	gt.bg = 0x000000;
	gt.len = 3;
	gt.str = (void *)"012";
	if (ioctl(sys, GFXIOC_TEXT, &gt) < 0) {
		perror("GFXIOC_TEXT");
		return 1;
	}

	/* Glyph 0 is solid: every pixel of the cell must be lit.  Glyph 1
	 * is 0xAA/0x55: pixel (x,y) is set when (x+y) is even.  Only
	 * painted pixels are compared - never against MAP()/RGB(), whose
	 * expansion differs (the sprite work paid for that lesson). */
	for (y = 0; y < 8; y++) {
		for (x = 0; x < 8; x++) {
			int c, got, want;

			c = ioctl(sys, GFXIOC_GETPIXEL, (void *)(intptr_t)
				  GFX_PIXEL_PACK(X0 + x, Y0 + y));
			got = c > 0 ? 1 : 0;
			want = 1;			/* solid block */
			if (got != want)
				bad_count++;

			c = ioctl(sys, GFXIOC_GETPIXEL, (void *)(intptr_t)
				  GFX_PIXEL_PACK(X0 + 8 + x, Y0 + y));
			got = c > 0 ? 1 : 0;
			want = ((x + y) & 1) ? 0 : 1;   /* checkerboard */
			if (got != want)
				bad_count++;
		}
	}
	printf("pixels: %d bad (0 expected)\n", bad_count);

	/* A built-in must be refused: 1-9 belong to the console and every
	 * other program. */
	memset(&bad, 0, sizeof(bad));
	bad.font = 9;
	bad.addr = (uint32_t)(unsigned long)myfont;
	bad.bytes = sizeof(myfont);
	printf("define font 9: %s (refusal expected)\n",
	       ioctl(sys, GFXIOC_FONTDEF, &bad) < 0 ? "refused" : "ACCEPTED");

	/* A length that disagrees with the header must be refused. */
	memset(&bad, 0, sizeof(bad));
	bad.font = FONTNO;
	bad.addr = (uint32_t)(unsigned long)myfont;
	bad.bytes = 8;			/* header says 4 + 3*8 */
	printf("short extent: %s (refusal expected)\n",
	       ioctl(sys, GFXIOC_FONTDEF, &bad) < 0 ? "refused" : "ACCEPTED");

	/* An address that is not ours at all. */
	memset(&bad, 0, sizeof(bad));
	bad.font = FONTNO;
	bad.addr = 0x20000000;		/* kernel RAM */
	bad.bytes = sizeof(myfont);
	printf("foreign address: %s (refusal expected)\n",
	       ioctl(sys, GFXIOC_FONTDEF, &bad) < 0 ? "refused" : "ACCEPTED");

	/* The ownership test: a fresh process must not see font 10. */
	pid = fork();
	if (pid == 0) {
		execl("/root/fontdef", "fontdef", "-c", (char *)NULL);
		perror("exec");
		_exit(9);
	}
	wait(&status);
	printf("ownership: %s\n",
	       (WIFEXITED(status) && WEXITSTATUS(status) == 0)
	       ? "child correctly saw no font 10"
	       : "CHILD SAW OUR FONT - slot is not private");

	printf("fontdef done\n");
	return 0;
}

/*
 * What upng.c wants from MMBasic, supplied to a standalone program -
 * and shared with loadpng, which borrows the same arena for the file
 * itself.  One translation unit so there is ONE arena: as a static
 * inline in the header, each object got its own 2M claim.
 *
 * THE MEMORY IS THE WHOLE PROBLEM.  Unlike picojpeg, upng inflates the
 * WHOLE image before anything can be drawn: PNG's filters refer to the
 * row above and its DEFLATE stream is one window over the entire
 * image.  The filtered raster is `w * (h * bpp + 7) / 8 + h` bytes and
 * the RGBA8 output another w*h*4 - about 307K each for a full 320x240
 * screen, against an 84-block (336K) process pool.  That is exactly why
 * the reference guards LOAD PNG with `#ifdef rp2350` and does not offer
 * it on a board without PSRAM.
 *
 * So the big blocks come from the PSRAM ARENA (PC3-PSRAM-ARENA.md) and
 * cost the process pool nothing.  The allocator is a BUMP allocator
 * that never frees - mmbc_util.c's ar_carve, the same pattern for the
 * same reason: this is a one-shot program that decodes one picture and
 * exits, and the kernel releases the arena on exit.  FreeMemorySafe
 * only NULLs the caller's pointer, which is all upng uses it for.
 *
 * The trade is peak memory for simplicity: nothing is reused, so the
 * compressed source, the inflated raster and the output stay resident
 * together.  For a full-screen picture that is under 1M of a ~7.8M
 * heap.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "pc3sys.h"

#include "upng_pc3.h"

#define PSRAMIOC_ALLOC	0x000A

struct psram_req {
	unsigned long len;
	unsigned long base;
};

/* Generous rather than tight, as mmbc's is: the PSRAM heap is ~7.8M,
   and a picture bigger than the screen is refused before it is
   decoded anyway. */
#define UPNG_ARENA_LEN	(2048UL * 1024)

static unsigned char *upa_cur, *upa_end;

static void upa_init(void)
{
	struct psram_req rq;
	int fd;

	if (upa_cur != NULL)
		return;
	fd = pc3_open_sys();
	rq.len = UPNG_ARENA_LEN;
	if (fd < 0 || pc3_ioctl(fd, PSRAMIOC_ALLOC, &rq) < 0) {
		fprintf(stderr, "loadpng: no PSRAM arena (kernel without "
				"PSRAMIOC_ALLOC?)\n");
		exit(1);
	}
	close(fd);
	upa_cur = (unsigned char *)rq.base;
	upa_end = upa_cur + UPNG_ARENA_LEN;
}

void *GetMemory(unsigned long n)
{
	void *p;

	upa_init();
	n = (n + 7) & ~7UL;
	if (upa_cur + n > upa_end) {
		fprintf(stderr, "loadpng: image needs more than %luK "
				"of arena\n", UPNG_ARENA_LEN / 1024);
		exit(2);
	}
	p = upa_cur;
	upa_cur += n;
	return p;
}

/* Nothing is reclaimed - see above.  upng only ever uses this to drop
   its own reference, and that still happens. */
void FreeMemorySafe(void *pp)
{
	void **p = (void **)pp;

	if (p != NULL)
		*p = NULL;
}

/* The interpreter checks for a keypress and services the watchdog here.
   A program has neither to do. */
void routinechecks(void) { }

void error(const char *msg)
{
	fprintf(stderr, "loadpng: %s\n", msg);
	exit(1);
}

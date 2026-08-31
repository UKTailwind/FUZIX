#ifndef MMB_FLASH_H
#define MMB_FLASH_H
/*
 *	FLASH DISK LOAD n, file$ [, O[VERWRITE]]
 *	FLASH ERASE n
 *	MM.INFO(FLASH ADDRESS n)
 *
 *	The PicoMite's image flash slots, without the flash: there is no
 *	XIP window to write on this machine, so a slot is memory, taken
 *	lazily on the first reference and filled with 0xFF - erased-flash
 *	semantics, which both "Already programmed" and BLIT FLASH's
 *	validity check read.  Under bcrun that memory is the VM heap,
 *	which is PSRAM: the same physical answer as the rp2350 PicoMite,
 *	arrived at by malloc.  A program that never names a slot pays
 *	nothing.
 *
 *	Slot CONTENT is the reference's, byte for byte: whatever file the
 *	program loads.  For images that means the PicoMite layout - two
 *	uint32 dimensions then packed 4bpp with the LOW nibble the left
 *	pixel, the mirror of this machine's own framebuffer packing - and
 *	the consumers (BLIT FLASH here, BLIT MEMORY by address) decode it
 *	that way, so asset files made for a PicoMite work unmodified.
 *	That is the entire point of keeping the commands.
 *
 *	The one recorded divergence (PLAN-games.md): slots live as long as
 *	the process, where real flash persists.  A program loads its own
 *	slots at startup; since a slot always starts erased, the guarded
 *	FLASH DISK LOAD pattern the reference requires works unchanged.
 *
 *	Reference: FileIO.c cmd_flash (DISK LOAD at :1232, slot bounds and
 *	"Already programmed" at :1239-1252), MM_Misc.c:8095 for the
 *	address, MAXFLASHSLOTS = 3 (configuration.h:318).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MMG_FN
#if defined(MM_FCC) || defined(MM_PC3)
#define MMG_FN static
#else
#define MMG_FN static __inline__ __attribute__((unused))
#endif
#endif

#define MMF_SLOTS 3
/* The reference slot size: MAX_PROG_SIZE, 120K on the rp2040 build.
 * It was 48K here on the theory that a slot only ever held a screen's
 * worth - and then PETSCII Robots loaded its 87,724-byte sprite
 * library into slot 3, exactly as it does on a Game*Mite.  Slots are
 * taken lazily from the VM heap (512K, PSRAM on the board), so a
 * program that never names one still pays nothing and the gates'
 * host runs carry the same default. */
#ifndef MMF_SLOTSZ
#define MMF_SLOTSZ (120L * 1024L)
#endif

static unsigned char *mmf_slot[MMF_SLOTS];
static long mmf_size[MMF_SLOTS];

/* The slot's base address, allocating on first reference - which is
 * what makes a program that mentions no slot cost nothing.  NULL only
 * after a raise.  The reference size is asked for first; an
 * environment whose heap cannot give it (the host gates' heap lives
 * inside mem[]) gets the largest halving it can, and a load still
 * raises "File too big" honestly against what was taken.  The board's
 * PSRAM heap grants the full reference size, which PETSCII Robots'
 * 87,724-byte sprite library needs. */
MMG_FN unsigned char *mmf_addr(MMINTEGER n)
{
	if (n < 1 || n > MMF_SLOTS)
		MM_RAISEV("Invalid flash slot", (unsigned char *)0);
	if (mmf_slot[n - 1] == NULL) {
		long sz = MMF_SLOTSZ;

		while (sz >= 4096
		       && (mmf_slot[n - 1] =
			   (unsigned char *)malloc((size_t)sz)) == NULL)
			sz /= 2;
		if (mmf_slot[n - 1] == NULL)
			MM_RAISEV("Not enough memory", (unsigned char *)0);
		mmf_size[n - 1] = sz;
		memset(mmf_slot[n - 1], 0xFF, (size_t)sz);
	}
	return mmf_slot[n - 1];
}

MMG_FN void mmf_erase(MMINTEGER n)
{
	unsigned char *s = mmf_addr(n);

	if (s != NULL)
		memset(s, 0xFF, (size_t)mmf_size[n - 1]);
}

/* file$ arrives as an MMBasic string; the runtime keeps the trailing
 * NUL that lets it go straight to fopen. */
MMG_FN void mmf_disk_load(const char *file, MMINTEGER n, MMINTEGER ovr)
{
	unsigned char *s = mmf_addr(n);
	FILE *f;
	long size;

	if (s == NULL)
		return;
	/* the reference tests the first flash WORD against erased; four
	 * explicit bytes, because long is 8 wide on the host gates */
	if (!ovr && (s[0] & s[1] & s[2] & s[3]) != 0xFF)
		MM_RAISE("Already programmed");
	f = fopen(mm_cstr(file), "rb");
	if (f == NULL)
		MM_RAISE("Cannot open file");
	fseek(f, 0L, SEEK_END);
	size = ftell(f);
	if (size > mmf_size[n - 1]) {
		fclose(f);
		MM_RAISE("File too big for a flash slot");
	}
	fseek(f, 0L, SEEK_SET);
	/* the erase the reference does */
	memset(s, 0xFF, (size_t)mmf_size[n - 1]);
	if (fread(s, 1, (size_t)size, f) != (size_t)size) {
		fclose(f);
		memset(s, 0xFF, (size_t)mmf_size[n - 1]);
		MM_RAISE("Cannot read file");
	}
	fclose(f);
}

/*
 *	FLASH LOAD IMAGE n, file$ [, O[VERWRITE]]   (FileIO.c:1030)
 *
 *	A BMP into a slot in the PicoMite's own layout - two little-endian
 *	uint32 for width and height, then packed 4bpp with the LOW nibble
 *	the left pixel - which is what BLIT FLASH and TILEMAP CREATE read.
 *	The reference decodes the file itself, a row at a time into flash;
 *	here the decoding is loadimage's, in another process, and the
 *	pixels come back down a pipe one index per byte (the -s form, the
 *	same protocol SPRITE LOADBMP uses) to be packed on the way in.
 *	A sprite needs no display, so neither does this.
 *
 *	The colour of a pixel is the reference's own SPRITE LOADBMP
 *	reduction - red's top bit, green's top two, blue's top bit - which
 *	is also what its FLASH LOAD IMAGE writes: both go through the same
 *	RGB121 packing of a decoded BMP.
 *
 *	"Already programmed" tests the first word against erased unless O
 *	is given, as the reference does.  A file whose rows would not fit
 *	the slot is refused whole rather than truncated; the reference
 *	writes on into the next slot.
 */
static unsigned char mmf_row[256];	/* unpacked pixels, a chunk at a time */

MMG_FN void mmf_load_image(const char *file, MMINTEGER n, MMINTEGER ovr)
{
	unsigned char *s = mmf_addr(n);
	unsigned char hdr[4];
	int fd, w, h, got, k, x, y, take;
	long stride;

	if (s == NULL)
		return;
	if (!ovr && (s[0] & s[1] & s[2] & s[3]) != 0xFF)
		MM_RAISE("Already programmed");
	mm_run_begin();
	mm_run_arg("\011loadimage");
	mm_run_arg("\002-s");
	mm_run_arg(file);
	fd = mm_run_pipe();
	if (fd < 0)
		return;			/* mm_run_pipe raised it */
	got = 0;
	while (got < 4) {
		k = (int)mm_run_pipe_read(fd, hdr + got, 4 - got);
		if (k <= 0)
			break;
		got += k;
	}
	if (got < 4) {
		if (mm_run_pipe_close(fd) < 0)
			return;		/* the decoder's own message */
		MM_RAISE("The BMP could not be decoded");
	}
	w = hdr[0] | (hdr[1] << 8);
	h = hdr[2] | (hdr[3] << 8);
	stride = (w + 1) >> 1;
	if (w < 1 || h < 1 || 8L + stride * h > mmf_size[n - 1]) {
		mm_run_pipe_close(fd);
		MM_RAISE("File too big for a flash slot");
	}
	/* the erase the reference does, then the header */
	memset(s, 0xFF, (size_t)mmf_size[n - 1]);
	s[0] = (unsigned char)w;
	s[1] = (unsigned char)(w >> 8);
	s[2] = 0;
	s[3] = 0;
	s[4] = (unsigned char)h;
	s[5] = (unsigned char)(h >> 8);
	s[6] = 0;
	s[7] = 0;
	for (y = 0; y < h; y++) {
		unsigned char *d = s + 8 + y * stride;

		/* a row in chunks of an even count, so a chunk boundary is
		 * a byte boundary in the slot; only the last may be odd */
		for (x = 0; x < w; x += take) {
			take = w - x;
			if (take > (int)sizeof(mmf_row))
				take = (int)sizeof(mmf_row);
			got = 0;
			while (got < take) {
				k = (int)mm_run_pipe_read(fd, mmf_row + got,
							  take - got);
				if (k <= 0)
					break;
				got += k;
			}
			if (got < take) {
				/* a half-written slot must not look like a
				 * good one, and the decoder's own message
				 * is the useful one, so close first */
				memset(s, 0xFF, (size_t)mmf_size[n - 1]);
				if (mm_run_pipe_close(fd) < 0)
					return;
				MM_RAISE("The BMP could not be decoded");
			}
			for (k = 0; k + 1 < take; k += 2)
				d[(x + k) >> 1] = (unsigned char)
					((mmf_row[k] & 15)
					 | ((mmf_row[k + 1] & 15) << 4));
			if (k < take)
				d[(x + k) >> 1] = (unsigned char)
					(0xF0 | (mmf_row[k] & 15));
		}
	}
	if (mm_run_pipe_close(fd) < 0) {
		memset(s, 0xFF, (size_t)mmf_size[n - 1]);
		return;			/* a failed decoder is an error */
	}
}

#endif /* MMB_FLASH_H */

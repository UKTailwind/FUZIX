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

#endif /* MMB_FLASH_H */

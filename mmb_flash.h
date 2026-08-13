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
/* The reference slot is MAX_PROG_SIZE (120K on the rp2040 build); here
 * a slot holds what the display can use - a full 320x240 sheet is
 * 38408 bytes - because the host gates' VM is 128K in total and a
 * faithful slot could never run there.  A larger file raises "File too
 * big for a flash slot" rather than truncating: the honest divergence,
 * recorded in PLAN-games.md.  Override with -DMMF_SLOTSZ if a real
 * asset ever needs it. */
#ifndef MMF_SLOTSZ
#define MMF_SLOTSZ (48L * 1024L)
#endif

static unsigned char *mmf_slot[MMF_SLOTS];

/* The slot's base address, allocating on first reference - which is
 * what makes a program that mentions no slot cost nothing.  NULL only
 * after a raise. */
MMG_FN unsigned char *mmf_addr(MMINTEGER n)
{
	if (n < 1 || n > MMF_SLOTS)
		MM_RAISEV("Invalid flash slot", (unsigned char *)0);
	if (mmf_slot[n - 1] == NULL) {
		mmf_slot[n - 1] = (unsigned char *)malloc(MMF_SLOTSZ);
		if (mmf_slot[n - 1] == NULL)
			MM_RAISEV("Not enough memory", (unsigned char *)0);
		memset(mmf_slot[n - 1], 0xFF, MMF_SLOTSZ);
	}
	return mmf_slot[n - 1];
}

MMG_FN void mmf_erase(MMINTEGER n)
{
	unsigned char *s = mmf_addr(n);

	if (s != NULL)
		memset(s, 0xFF, MMF_SLOTSZ);
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
	if (size > MMF_SLOTSZ) {
		fclose(f);
		MM_RAISE("File too big for a flash slot");
	}
	fseek(f, 0L, SEEK_SET);
	memset(s, 0xFF, MMF_SLOTSZ);	/* the erase the reference does */
	if (fread(s, 1, (size_t)size, f) != (size_t)size) {
		fclose(f);
		memset(s, 0xFF, MMF_SLOTSZ);
		MM_RAISE("Cannot read file");
	}
	fclose(f);
}

#endif /* MMB_FLASH_H */

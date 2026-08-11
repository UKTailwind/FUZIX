#ifndef GLOBALS_H
#define GLOBALS_H

/*
 *	Where the dhara flash disk begins, and therefore the hard ceiling
 *	on the kernel image's flash footprint.
 *
 *	This was 96K, and the kernel quietly grew past it.  The result
 *	was the nastiest failure mode this port has had: dhara's resume
 *	found the kernel's own code where its journal should be, "repaired"
 *	the journal by erasing those blocks - on the FIRST boot after
 *	flashing - and the ROM's next attempt to load the image found it
 *	mutilated.  The board then looked bricked: reset and power cycle
 *	both dead (the damage is persistent), only a reflash reviving it,
 *	and that only until the next boot re-ate the tail.
 *
 *	1M now, raised from 512K when the flash root stopped fitting: the
 *	image is ~160K, so this is six-fold headroom, and nothing else
 *	competes for the space - the kernel and the flash disk are the
 *	only two things in this chip.  CMakeLists fails the build outright
 *	if fuzix.bin ever reaches this offset, so the overlap that caused
 *	the damage above can never come back silently.
 *
 *	FOUR THINGS MOVE TOGETHER.  This constant, the `-o' offset the
 *	Makefile converts filesystem.uf2 to, `mkftl -s' which must describe
 *	the same device devflash.c does, and PICO_FLASH_SIZE_BYTES in
 *	CMakeLists.txt:
 *
 *		disk size = PICO_FLASH_SIZE_BYTES - FLASH_OFFSET
 *
 *	PICO_FLASH_SIZE_BYTES is set to 16M there because a PC2 and a PC3
 *	always carry 16M; the pico2 board header defaults to 4M, which had
 *	quietly confined the disk to the first quarter of the chip.
 *
 *	The uf2 offset had been left at the old 0x10018000 - 96K, the
 *	value from before the bricking above - so flashing filesystem.uf2
 *	would have written the disk straight over the kernel image and
 *	reproduced exactly that failure.  Nobody noticed because that uf2
 *	is not a release asset and the SD card is the real root.
 */
#define FLASH_OFFSET (1024*1024)

extern void flash_dev_init(void);
extern void sd_rawinit(void);

extern void contextswitch(ptptr p);

struct svc_frame
{
	uint32_t r12;
	uint32_t pc;
	uint32_t lr;
	uint32_t r0;
	uint32_t r1;
	uint32_t r2;
	uint32_t r3;
};

struct exception_frame
{
	uint32_t r0;
	uint32_t r1;
	uint32_t r2;
	uint32_t r3;
	uint32_t r12;
	uint32_t lr;
	uint32_t pc;
	uint32_t psr;
};

struct extended_exception_frame
{
	uint32_t r8;
	uint32_t r9;
	uint32_t r10;
	uint32_t r11;
	uint32_t cause;
	uint32_t sp;
	uint32_t r4;
	uint32_t r5;
	uint32_t r6;
	uint32_t r7;
	uint32_t r0;
	uint32_t r1;
	uint32_t r2;
	uint32_t r3;
	uint32_t r12;
	uint32_t lr;
	uint32_t pc;
	uint32_t psr;
};

#endif


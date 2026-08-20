#ifndef GLOBALS_H
#define GLOBALS_H

/*
 *	There is no flash disk any more, so there is no FLASH_OFFSET and
 *	no ceiling on the kernel image: the kernel is the only thing in
 *	this chip.  See config.h for why the device went.
 *
 *	Worth keeping the story, because it is the best argument the port
 *	has for not putting a filesystem in the same flash as the code.
 *	FLASH_OFFSET was 96K once, and the kernel quietly grew past it.
 *	dhara's resume found the kernel's own code where its journal
 *	should be, "repaired" the journal by erasing those blocks - on the
 *	FIRST boot after flashing - and the ROM's next attempt to load the
 *	image found it mutilated.  The board then looked bricked: reset
 *	and power cycle both dead, only a reflash reviving it, and that
 *	only until the next boot re-ate the tail.  It was raised to 1M and
 *	guarded by a build-time check after that; now the whole class of
 *	failure is gone with the device.
 */

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


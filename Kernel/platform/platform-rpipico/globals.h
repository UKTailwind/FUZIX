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
 *	512K leaves the ~100K image five-fold headroom, and CMakeLists
 *	fails the build outright if fuzix.bin ever reaches this offset,
 *	so the overlap can never come back silently.
 */
#define FLASH_OFFSET (512*1024)

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


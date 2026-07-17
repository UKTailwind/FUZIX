/* Enable to make ^Z dump the inode table for debug */
#undef CONFIG_IDUMP
/* Enable to make ^A drop back into the monitor */
#undef CONFIG_MONITOR
/* Profil syscall support (not yet complete) */
#undef CONFIG_PROFIL
/* Multiple processes in memory at once */
#undef CONFIG_MULTI

/* Select a banked memory set up */
#define CONFIG_BANK16
/* This is the number of banks of user memory available (maximum) */
#define MAX_MAPS	252		/* 256 x 16K pages - 4 for kernel? */
/* How many banks do we have in our address space */
#define CONFIG_BANKS	4	/* 4 x 16K */

/* Video terminal support */
#define CONFIG_VT
#define CONFIG_VT_MULTI
#define VT_WIDTH    80
#define VT_HEIGHT   30
#define VT_RIGHT    79
#define VT_BOTTOM   29

/*
 *	Define the program loading area (needs to match kernel.def)
 */
#define PROGBASE    0x0000  /* Base of user  */
#define PROGLOAD    0x0100  /* Load and run here */
#define PROGTOP     0xF000  /* Top of program, base of U_DATA stash */
#define KERNTOP     0xC000  /* Top of kernel, first 3 banks */
#define PROC_SIZE   64 	    /* Memory needed per process including stash */

#define PTABSIZE    48

/* Networking - disabled for now */
/*#define CONFIG_NET
#define CONFIG_NET_NATIVE*/

/*
 *	Definitions for swapping - disabled for now
 */

/* #define SWAPDEV     (swap_dev)*/	/* A variable for dynamic, or a device major/minor */
//extern uint16_t swap_dev;
#undef SWAPDEV
//#define SWAP_SIZE   0x78 	/* Program +udata in blocks */
//#define SWAPBASE    0x0000	/* We swap the lot in one, include the */
//#define SWAPTOP	    0xF000	/* vectors so its a round number of sectors */

//#define MAX_SWAPS	16	/* Maximum number of swapped out processes. */

/*
 *	When the kernel swaps something it needs to map the right page into
 *	memory using map_for_swap and then turn the user address into a
 *	physical address. For a simple banked setup there is no conversion
 *	needed so identity map it.
 */
#define swap_map(x) ((uint8_t *)((((x) & 0x3FFF)) + 0x4000))

#define BOOTDEVICENAMES "hd#"

/* We will resize the buffers available after boot. This is the normal setting */
#define CONFIG_DYNAMIC_BUFPOOL
/* Swap will be set up when a suitably labelled partition is seen */
//#define CONFIG_DYNAMIC_SWAP

/* Larger transfers (including process execution) should go directly not via
   the buffer cache. For all small (eg bit) systems this is the right setting
   as it avoids polluting the small cache with data when it needs to be full
   of directory and inode information */
/*#define CONFIG_LARGE_IO_DIRECT(x)	1*/

#define CONFIG_RTC
#define CONFIG_RTC_INTERVAL	1

/*
 * How fast does the clock tick (if present), or how many times a second do
 * we simulate if not. For a machine without video 10 is a good number. If
 * you have video you probably want whatever vertical sync/blank interrupt
 * rate the machine has. For many systems it's whatever the hardware gives
 * you.
 *
 * Note that this needs to be divisible by 10 and at least 10. If your clock
 * is a bit slower you may need to fudge things somewhat so that the kernel
 * gets 10 timer interrupt calls per second. 
 */
#define TICKSPERSEC 100	    /* Ticks per second */

/*
 *	The device (major/minor) for the console and boot up tty attached to
 *	init at start up. 512 is the major 2, so all the tty devices are
 *	512 + n where n is the tty.
 */
#define BOOT_TTY (512 + 1)      

#define CMDLINE	0x81	  /* CP/M commandline */

/* Device parameters */
#define NUM_DEV_TTY 6	  /* How many tty devices does the platform support */
#define TTYDEV   BOOT_TTY /* Device used by kernel for messages, panics */
#define NBUFS    5        /* Number of block buffers. Must be 4+ and must match
                             kernel.def */
#define NMOUNTS	 4	  /* Number of mounts at a time */

#define CONFIG_SMALL

#define plt_copyright()

/* Enable to make ^Z dump the inode table for debug */
#undef CONFIG_IDUMP
/* Enable to make ^A drop back into the monitor */
#undef CONFIG_MONITOR
/* Profil syscall support (not yet complete) */
#undef CONFIG_PROFIL
/* Multiple processes in memory at once */
#define CONFIG_MULTI
/* Select a banked memory set up */
#define CONFIG_BANK_FIXED
/* This is the number of banks of user memory available (maximum) */
#define MAX_MAPS	30	/* 1MB minus the kernel */
/* How big is each bank - in our case 32K, 48K is actually more common. This
   is hardware dependant */
#define MAP_SIZE	0x8000
/* How many banks do we have in our address space */
#define CONFIG_BANKS	2	/* 2 x 32K */
/* Permit large I/O requests to bypass cache and go direct to userspace */
#define CONFIG_LARGE_IO_DIRECT(x)	1
#define TICKSPERSEC 20		/* Ticks per second */
#define PROGBASE    0x0000	/* also data base */
#define PROGLOAD    0x0100	/* also data base */
#define PROGTOP     0x7E00	/* Top of program */
#define PROC_SIZE   32		/* Memory needed per process */

#define SWAPDEV     (swap_dev)	/* A variable for dynamic, or a device major/minor */
extern uint16_t swap_dev;
#define SWAP_SIZE   0x40 	/* 32K in blocks (prog + udata) */
#define SWAPBASE    0x0000	/* start at the base of user mem */
#define SWAPTOP	    0x8000	/* Swap out program */
#define MAX_SWAPS   32	    	/* We will size if from the partition */
/* Swap will be set up when a suitably labelled partition is seen */
#define CONFIG_DYNAMIC_SWAP
#define MAXTICKS    20		/* As we are pure swap */

/*
 *	When the kernel swaps something it needs to map the right page into
 *	memory using map_for_swap and then turn the user address into a
 *	physical address. For a simple banked setup there is no conversion
 *	needed so identity map it.
 */
#define swap_map(x)	((uint8_t *)(x))

/* We need a tidier way to do this from the loader */
#define CMDLINE	NULL  /* Location of root dev name */
#define BOOTDEVICENAMES "hd#,fd"

#define CONFIG_DYNAMIC_BUFPOOL /* we expand bufpool to overwrite the _DISCARD segment at boot */
#define NBUFS    4        /* Number of block buffers, keep in line with space reserved in zeta-v2.s */
#define NMOUNTS	 2	  /* Number of mounts at a time */

#define CONFIG_TD_NUM		4
#define CONFIG_TD_SCSI

/* Device parameters */
#define NUM_DEV_TTY 2

/* UART0 as the console */
#define BOOT_TTY (512 + 1)
#define TTY_INIT_BAUD B9600	/* Usual ROM default */

#define TTYDEV   BOOT_TTY /* Device used by kernel for messages, panics */

#define plt_copyright()

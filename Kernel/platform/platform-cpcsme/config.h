/* Enable to make ^Z dump the inode table for debug */
#undef CONFIG_IDUMP
/* Enable to make ^A drop back into the monitor */
#undef CONFIG_MONITOR
/* Profil syscall support (not yet complete) */
#undef CONFIG_PROFIL
/* Multiple processes in memory at once */
#define CONFIG_MULTI
/* Fixed banking */
#define CONFIG_BANK_FIXED
/* CP/M emulation */
#undef CONFIG_CPM_EMU

#define CONFIG_SMALL

/* Input layer support */
#define CONFIG_INPUT
#define CONFIG_INPUT_GRABMAX	3
/* Video terminal, not a serial tty */
#define CONFIG_VT
/* Keyboard contains non-ascii symbols */
#define CONFIG_UNIKEY
/* maximum supported is 1088KiB -> 17 64KiB pages, one is kernel, one is video, 15 for user*/
#define MAX_MAPS	15
#define MAP_SIZE	0xF000U

/* Banks as reported to user space */
#define CONFIG_BANKS	1
/*
#define MAP_TRANS_8TO16(M)	(uint16_t)(M) + (((M) & (1 << 6)) ? 0x7F00 : 0x7E40)
#define MAP_TRANS_16TO8(M)	(uint8_t)(((M) - (((M) & 0x7F00) == 0x7F00) ? 0x7F00 : 0x7E40))
*/
/* Vt definitions */
#define VT_WIDTH	80
#define VT_HEIGHT	25
#define VT_RIGHT	79
#define VT_BOTTOM	24
#define CONFIG_VT_MULTI
#define MAX_VT	4

#define TICKSPERSEC 50   /* Ticks per second */
#define PROGBASE    0x0000  /* also data base */
#define PROGLOAD    0x0100  /* also data base */
#define PROGTOP     0xF000  /* Top of program, base of U_DATA copy */
#define PROC_SIZE   60	    /* Memory needed per process */

#define CONFIG_SWAP
#ifdef CONFIG_SWAP
    #define SWAPBASE 0x0000
    #define SWAPTOP  0xF400UL
    #define SWAP_SIZE 0x7A		/*process 60K + udata + udata copy*/
    #define MAX_SWAPS	13 
    #define PTABSIZE    16
    #define CONFIG_DYNAMIC_SWAP
    #define SWAPDEV  (swap_dev)  /* Device for swapping (dynamic). */
#endif


/* We swap by hitting the user map */
#define swap_map(x)		((uint8_t *)(x))

#define BOOT_TTY (512 + 1)/* Set this to default device for stdio, stderr */
                          /* In this case, the default is the first TTY device */

/* We need a tidier way to do this from the loader */
#define CMDLINE	NULL	  /* Location of root dev name */

/* Device parameters */
#undef CONFIG_USIFAC_SERIAL
#ifdef CONFIG_USIFAC_SERIAL
    #define NUM_DEV_TTY 5
    #define TTY_INIT_BAUD B115200	/*USIFAC*/
#else
    #define NUM_DEV_TTY 4
#endif


/* SLIP on Usifac */
#undef CONFIG_USIFAC_SLIP
#ifdef CONFIG_USIFAC_SLIP
    #ifdef CONFIG_USIFAC_SERIAL
        /* Core networking support */
        #define CONFIG_NET
        #define CONFIG_NET_NATIVE
    #else
        #undef CONFIG_USIFAC_SLIP
    #endif
#endif

#define TTYDEV   BOOT_TTY /* Device used by kernel for messages, panics */
#define NBUFS    8	  /* Number of block buffers MUST be big enough to push discard above 0xC000*/
                        /*Remember to set it also in kernel.def*/
#define NMOUNTS	 4	  /* Number of mounts at a time */

#define CONFIG_DYNAMIC_BUFPOOL
#define CONFIG_LARGE_IO_DIRECT(x)	1

#define CONFIG_FDC765
#define CONFIG_TD
#define CONFIG_TD_NUM	4
/* IDE/CF support */
#define CONFIG_TD_IDE
#ifdef CONFIG_TD_IDE
    #define CONFIG_TINYIDE_SDCCPIO
    #define CONFIG_TINYIDE_8BIT
    #define IDE_IS_8BIT(x)		1
#endif
/* Albireo ch376 USB storage support*/
#define CONFIG_ALBIREO
/* USIFAC/ULIFAC ch376 USB storage support*/
#define CONFIG_USIFAC_CH376
#ifdef CONFIG_USIFAC_CH376
    #undef CONFIG_USIFAC_SERIAL
    #undef CONFIG_USIFAC_SLIP
    #undef CONFIG_NET
    #undef CONFIG_NET_NATIVE
#endif

#if ((defined CONFIG_ALBIREO)||(defined CONFIG_USIFAC_CH376))
    #define CONFIG_CH375
#endif

/* Net4CPC card */
#undef CONFIG_NET4CPC
#ifdef CONFIG_NET4CPC
    /* Core networking support */
    #define CONFIG_NET
    #define CONFIG_NET_WIZNET
    #define CONFIG_NET_W5100
#endif

#define BOOTDEVICENAMES "hd#,fd"

#ifndef CONFIG_H
#define CONFIG_H
/* NOTE: tusb_config.h is included further down, after the board block:
 * its host/device mode choice tests CONFIG_PC3_DISPLAY, which must be
 * defined first. */
/*
 * Set this according to your SD card pins
 *  CONFIG_RC2040
 *      SCK GPIO 14
 *      TX  GPIO 15
 *      RX  GPIO 12
 *      CS  GPIO 13
 *  CONFIG_MAKER_PI
 *	    SCK GPIO 10
 *	    TX  GPIO 11
 *	    RX  GPIO 12
 *	    CS  GPIO 15
 *  CONFIG_PICOCALC
 *	    SCK GPIO 18
 *	    TX  GPIO 19
 *	    RX  GPIO 16
 *	    CS  GPIO 17
 *  CONFIG_PICO_COMPUTER_3
 *	    SCK GPIO 30
 *	    TX  GPIO 31
 *	    RX  GPIO 28
 *	    CS  GPIO 33
 *  If Undefined
 *      SCK GPIO 2
 *      TX  GPIO 3
 *      RX  GPIO 4
 *      CS  GPIO 5
 */

#define CONFIG_PICO_COMPUTER_3

/* Pico Computer 3 clocking: 315 MHz (DVDD comes from the board's external
 * 1.3 V regulator, so no vreg change; clk_peri follows clk_sys as in the
 * PC3 MicroPython/MMBasic firmwares). The flash QMI clock is capped at
 * 63 MHz so the divisor (= RXDELAY, a 3-bit field) stays valid: 315/63 ->
 * div 5. PSRAM is 8 MiB on QMI CS1, GP47. */
#ifdef CONFIG_PICO_COMPUTER_3
/* 375 MHz: MMBasic's FreqXGA, the PC3-proven XGA clock.  Graphics
 * modes run HSTX at clk_sys (full-rate DDR): pixel clock 75 MHz =
 * VESA 1024x768 at 70.07 Hz on the 1328x806 frame.  The text console
 * runs clk_hstx = clk_sys/3: 25 MHz pixel = 640x480 at 59.5 Hz.
 * Flash QMI 62.5 MHz (div 6), PSRAM 125 MHz (div 3), UART/SD divisors
 * all derived at runtime from the actual clk_sys. */
#define PC3_SYS_CLOCK_KHZ 375000
#define PC3_FLASH_MAX_HZ (63 * 1000 * 1000)
#define PC3_PSRAM_CS_PIN 47

/* DS3231 RTC on I2C0 (GP20/21): time loads at boot via setdate in rc;
 * resync the tick clock from it every 5s (interval is in deciseconds). */
#define CONFIG_RTC
#define CONFIG_RTC_FULL
#define CONFIG_RTC_INTERVAL 50

/* 64 processes.
 *
 * This was 30, chosen when swap was a fixed slot and MAX_SWAPS bounded
 * how many processes could be out at once.  Neither is true now: a
 * swapped process gets a PSRAM arena allocation of exactly its own
 * size (swapper.c), and resident memory is packed in 4K chunks at
 * actual size, so the table no longer reserves anything on behalf of a
 * process that does not exist.  swapmap/MAX_SWAPS in Kernel/swap.c is
 * a free list of fixed slots this platform stopped using.
 *
 * What it does cost is the table itself: p_tab is 96 bytes, so 64
 * entries are 6,144 against 30's 2,880, plus 4 bytes each of swapaddr.
 * About 3.4K of the kernel's headroom, which is affordable now that
 * most of the kernel executes from flash.
 *
 * The companion tables scale with it - every background job holds
 * open-file and inode slots, and the stock 15/20 ran dry a few
 * processes past the old limit. */
#define PTABSIZE 64
/* Sized for 64 processes, not guessed: the shell gives every background
 * job its own /dev/null on stdin, so the open-file table needs an entry
 * per process before anything opens a real file.  At 48 it ran out at
 * about 45 jobs - "cannot open /dev/null" and then ENFILE - which is
 * what the old comment about companion tables was describing.  12 bytes
 * an entry here and 80 an inode, so this costs under 3K. */
#define OFTSIZE 128
#define ITABSIZE 64

/* HDMI display on HSTX: core1 is owned by the scanout, so the USB-device
 * console loop it used to run is disabled (the PC3's USB port faces the
 * on-board hub and is destined for host mode anyway). console.c renders
 * an 80x40 ANSI console onto it (the kernel vt layer is VT52 and stays
 * out of the build). */
#define CONFIG_PC3_DISPLAY

/* USB host keyboard.  (To bisect TinyUSB out entirely - no init, no
 * pump, serial-only input with the PendSV machinery still running -
 * undefine CONFIG_PC3_USB_KBD.)
 *
 * The SE0 bus reset at usbkbd_init is ENABLED.  The PC3's CH334 hub is
 * externally powered: it keeps its USB address and configured state
 * across a warm reboot and then ignores re-enumeration from address 0,
 * so without the reset the keyboard only ever works after a full power
 * cycle - the exact field report that got it turned back on.  It had
 * been disabled by PC3_NO_USB_BUS_RESET from v0.5 to v0.14, by a
 * commit whose stated reason was lost to a truncated message; if a
 * problem reappears, suspect booting with the DPDT switch in the PROG
 * position (the SE0 is then driven at the attached PC, not the hub)
 * and record the finding HERE before defining it again. */
#define CONFIG_PC3_USB_KBD

/* BBC 4-channel sound on the PCM5102 I2S DAC (GP10/11/22) */
#define CONFIG_PC3_SOUND
#endif

#include "tusb_config.h"

/* We have a GPIO interface */
#define CONFIG_DEV_GPIO
/* /dev/i2c, minor 7 of the sys device: userland on I2C0, which is the
   QWIIC socket.  i2cuser.c has the sharing argument; the short form is
   that a non-preemptive kernel makes a transaction atomic for free, and
   the hourly RTC poll in interrupt context skips rather than waits. */
#define CONFIG_DEV_I2C
/* Ownership of the I/O header's pins and the peripheral blocks behind
   them, so userland can drive them DIRECTLY (there is no MMU here) with
   the kernel keeping only the arbitration and the cleanup.  pinlock.c
   has the argument; PC3-IO-PLAN.md has the plan it belongs to. */
#define CONFIG_PC3_PINLOCK
/* SPI0 for userland - MMBasic's SPI, on header pins.  SPI1 is NOT
   offered: it is the SD card's, and a program that could take it could
   take the filesystem out from under itself.  spiuser.c. */
#define CONFIG_PC3_SPI0
/* Enable to make ^Z dump the inode table for debug */
#undef CONFIG_IDUMP
/* Enable to make ^A drop back into the monitor */
#undef CONFIG_MONITOR
/* Enable to support network stack */
#undef CONFIG_NET
#undef CONFIG_NET_NATIVE
/* Profil syscall support (not yet complete) */
#undef CONFIG_PROFIL
/* Multiple processes in memory at once */
#define CONFIG_MULTI
/* 32bit with flat memory */
#undef CONFIG_FLAT
/* The platform validates syscall buffer addresses itself (arena.c):
 * the stock valaddr accepts only the process image, and a buffer in a
 * PSRAM arena the process owns is equally legitimate - without this,
 * read() into an arena is EFAULT and the facility cannot do I/O. */
#define CONFIG_CUSTOM_VALADDR
/* "#!" scripts exec the platform's fixed interpreter (misc.c: bcrun),
 * which is how the C compiler's output runs as ./prog */
#define CONFIG_SCRIPT_INTERP
/* exec calls plt_exec_cleanup() once committed: PSRAM arenas do not
 * survive into the new image (exec's pagemap_realloc gets a NULL hdr
 * on this port, so the release cannot key off that) */
#define CONFIG_PLT_EXEC_CLEANUP
/* Pure swap */
#define CONFIG_BANKS 1
/* brk() calls pagemap_realloc() to get more memory. */
#define CONFIG_BRK_CALLS_REALLOC
/* Inlined irq handling */
#define CONFIG_INLINE_IRQ
/* Trim disk blocks when no longer used */
#define CONFIG_TRIM
/* Enable single tasking */
#define CONFIG_SWAP_ONLY
#define CONFIG_SPLIT_UDATA
/* Enable SD card code. */
#define CONFIG_SD
#define SD_DRIVE_COUNT 1
/* Enable dynamic swap. */
#define CONFIG_PLATFORM_SWAPCTL
/* Platform manages process brk. */
#define CONFIG_PLATFORM_BRK
/* Platform IOCTL on /dev/sys (maj:min)(4:6) */
#define CONFIG_DEV_PLATFORM

#define CONFIG_32BIT
#define CONFIG_USERMEM_DIRECT
/* Serial TTY, no VT or font (the PC3 console is its own ANSI engine) */
#undef CONFIG_VT
#undef CONFIG_FONT8X8

/*
 * Built in NAND flash (/dev/hda, dhara over the XIP flash).
 *
 * OFF, and this is why it was "unstable". rawflash.c calls
 * flash_range_erase/flash_range_program guarded by nothing but di(),
 * and on RP2350 at this clock those calls do two things to the rest of
 * the machine:
 *
 *  - boot2 re-runs inside them and leaves QMI M0 at CLKDIV=2. At
 *    375MHz that is ~189MHz flash SPI, well above spec, and it is not
 *    restored afterwards. MMBasic wraps every call to save and restore
 *    qmi_hw->m[0].timing/rfmt - and m[1] with it, which is the PSRAM.
 *
 *  - they invalidate the XIP cache. psram.c sets XIP_CTRL_WRITABLE_M1,
 *    so PSRAM is cached write-back, which means the swap device's
 *    dirty lines live in that cache. Invalidating without cleaning
 *    first throws them away. MMBasic walks XIP_MAINTENANCE_BASE over
 *    16K to commit them before it lets that happen; we do not clean
 *    that cache anywhere at all.
 *
 * Both are now handled in rawflash.c, following MMBasic's FileIO.c
 * rather than re-deriving them.
 *
 * Left ON deliberately even though root is on SD and swap is in PSRAM,
 * so this device earns little: turning it off renumbers every block
 * device - the SD moves from hdb to hda and the PSRAM disc from hdc to
 * hdb - which silently breaks "swapon /dev/hdc" in rc, every bootdev
 * habit, and the manual. Not worth it for a device that is now safe.
 */
#define CONFIG_PICO_FLASH

/* Program layout */

#define UDATA_BLKS  3
#define UDATA_SIZE  (UDATA_BLKS << BLKSHIFT)

#ifndef TOTALMEM
#define TOTALMEM 160
#endif
#if TOTALMEM == 0
#error TOTALMEM should have been defined via cmake
#endif
#define NETMEM 0

#ifdef CONFIG_NET
#undef NETMEM
#define NETMEM 10
#endif

#define USERMEM ((TOTALMEM-NETMEM)*1024)

/* The process ceiling is now all of user memory (Pico Computer 3).
 * Resident memory is packed at actual size in 4K chunks, so small
 * processes still cost what they always did; what changed is that swap
 * is a PSRAM allocation the size of the process rather than a fixed
 * slot, so nothing forces a limit below the address space a process can
 * actually be resident in.  The old 262144 was the slot size - an
 * 8-bit machine's decision - and it stopped bcrun loading a 140K
 * translated BASIC program on a 312K machine with 8 MiB of PSRAM.
 *
 * A process this big leaves nothing else resident.  That is fine: the
 * others swap, and fork stages the parent into PSRAM rather than
 * needing two copies in RAM at once (see swapper.c). */
#define PROGSIZE (USERMEM - UDATA_SIZE)
extern uint8_t progbase[USERMEM];
#define udata (*(struct u_data*)progbase)

/* 8K: the C stack is a fixed window between BSS and heap; BBC BASIC's
 * recursive expression evaluator needs the headroom (it guards its own
 * depth against this figure minus a margin). */
#define USERSTACK (8*1024)

/* A program may ask for more with ld -z stack-size=N, recorded in its
 * PT_GNU_STACK header and honoured by the ELF loader; this caps what
 * it can take out of its own 256K.  The compiler passes need it: cc1
 * and cc2 are recursive-descent over expression trees, and a large
 * generated program (a 3000-line BASIC translation) drives them deeper
 * than 8K of frames.  Overflowing the window runs into BSS silently,
 * which is how it presented - a wild pointer and a dead machine. */
#define USERSTACK_MAX (64*1024)

#define CONFIG_CUSTOM_VALADDR
#define PROGBASE ((uaddr_t)&progbase[0])
#define PROGLOAD ((uaddr_t)&progbase[UDATA_SIZE])
#define PROGTOP (PROGLOAD + PROGSIZE)
#define SWAPBASE PROGBASE
#define SWAPTOP (PROGBASE + (uaddr_t)alignup(udata.u_break - PROGBASE, 1<<BLKSHIFT)) /* never swap in/out data above break */
#define SWAP_SIZE   ((PROGSIZE >> BLKSHIFT) + UDATA_BLKS)
#define MAX_SWAPS   (16384 / SWAP_SIZE) /* for the 8MB PSRAM swap disc */

#define BOOT_TTY (512 + 1)   /* Set this to default device for stdio, stderr */
                          /* In this case, the default is the first TTY device */

#define TICKSPERSEC 200   /* Ticks per second */
/* 
 * Boot cmd line.
 * [BOOTDEVICE] [tty=<TTYLIST>]
 * 
 * <BOOTDEVICE> - use `hda` for built-in flash or `hdbX` for SD card, where X is partition number
 * <TTYLIST> - list of TTY devices in order. If not specified system will
 *      map USB devices to tty1-4 and UART0 to tty5 if USB is connected. Or UART0 to tty1 etc if not.
 *      Example: `tty=usb1,uart1,usb2`
*/
#define CMDLINE	NULL	  /* Location of root dev name */

#define BOOTDEVICENAMES "hd#"
#define SWAPDEV    (swap_dev) /* dynamic swap */

/* Device parameters */
#define NUM_DEV_TTY_UART 2 /* min 1 max 2*/
/* Pico Computer 3: the console is the CH340 (USB-C serial) wired to the
 * uart1 peripheral, GP8=TX / GP9=RX. 115200 like the other PC3 firmwares
 * (the kernel default termios would drop the port to 9600 at tty open). */
#define TTY_INIT_BAUD B115200
#define DEV_UART_0_INSTANCE 1
#define DEV_UART_0_TX_PIN 8
#define DEV_UART_0_RX_PIN 9
/* Second serial port on the I/O header: /dev/tty2 = uart0, GP0=TX /
 * GP1=RX, no flow control.  BBC BASIC reaches it as a port channel:
 * ch% = OPENUP("/dev/tty2") then BGET#/BPUT#; baud via *stty. */
#define DEV_UART_1_INSTANCE 0
#define DEV_UART_1_TX_PIN 0
#define DEV_UART_1_RX_PIN 1
#define DEV_UART_1_CTS_PIN -1
#define DEV_UART_1_RTS_PIN -1
#define NUM_DEV_TTY_USB 4 /* min 1 max 4. */
#define NUM_DEV_TTY (NUM_DEV_TTY_UART + NUM_DEV_TTY_USB)
#ifdef CONFIG_PC3_DISPLAY
#define DEV_USB_DETECT_TIMEOUT 0 /* USB device stack never runs: don't wait */
#else
#define DEV_USB_DETECT_TIMEOUT 5000 /* (ms) Total timeout time to detect USB host connection*/
#endif
#define DEV_USB_INIT_TIMEOUT 2000 /* (ms) Total timeout to try not swallow messages */

#define TTYDEV   BOOT_TTY /* Device used by kernel for messages, panics */
#define NBUFS    20       /* Number of block buffers */
#define NMOUNTS	 4	  /* Number of mounts at a time */

/* Check the superblock's invariants at every filesystem operation and
 * panic naming the field that is wrong.  Filesystem corruption here has
 * twice been found only by fsck afterwards, with nothing left to say
 * what did it; this stops the machine at the first operation after the
 * damage instead.  Costs a scan of two 50 entry arrays per operation
 * and no memory.  See sb_validate() in filesys.c. */
#define CONFIG_SB_TRIPWIRE

/* Assert the two things blk_op's single global cannot survive: a
 * transfer starting inside another one, and swap I/O aimed anywhere but
 * the swap device.  See the tripwire note in dev/blkdev.c. */
#define CONFIG_BLK_TRIPWIRE

#define MAX_BLKDEV	4

#define CONFIG_SMALL

/*
 * The Pico Computer 3 release, which is NOT the same number as FUZIX's.
 *
 * start.c prints "FUZIX version 0.5" from Kernel/version.c - upstream's
 * own version, and correct - so a user who downloaded pc3-v0.6 and
 * booted it saw 0.5 and reasonably concluded the wrong file had been
 * published.  Both numbers are right; only one of them was on screen.
 *
 * Bump this when tagging a release; BUILDING-PC3.md says so too.
 * It is not the only place the release number is written down -
 * MM_RELEASE in mmb_runtime.h is what a BASIC program's MM.VER
 * answers, and it sat three releases behind this one without anything
 * noticing.  `sh relcheck.sh` now compares the two and the manual.
 */
#define PC3_RELEASE "0.16"

/*
 * The port's own copyright line.  It goes here rather than in start.c's
 * list because that list is printed by every platform, and this work is
 * in one of them; keeping it in the platform hook also keeps it out of
 * the way of merges from upstream.
 */
#define plt_copyright() \
	kprintf("Pico Computer 3, release %s (on FUZIX %s)\n" \
		"Copyright (c) 2026 Peter Mather\n", \
		PC3_RELEASE, sysinfo.uname)
#define swap_map(x) ((uint8_t*)(x))

/* Prevent name clashes wish the Pico SDK */

#define MANGLED 1
#include "mangle.h"

#endif
// vim: sw=4 ts=4 et


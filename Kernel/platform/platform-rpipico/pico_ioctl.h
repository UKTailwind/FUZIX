#ifndef PICO_IOCTL_H
#define PICO_IOCTL_H

#include <stdint.h>

/* Reboot PI Pico into flash mode */
#define PICOIOC_FLASH 0x0001

/* Set the USB keyboard layout: data -> a 2-letter layout name
 * (US/UK/DE/FR/ES/BE, case-insensitive) */
#define PICOIOC_KBDMAP 0x0002

/* Re-drive an SE0 bus reset on the root USB port, forcing any attached
 * hub back to Default state so it re-enumerates.  The kernel does this
 * once at boot; this is for when a hub appears AFTER boot already
 * configured - on the PC3 that means flipping the DPDT switch from the
 * programming port to the hub, which presents a hub that has never
 * lost VBUS and so ignores enumeration from address 0.  Needed because
 * hcd_port_reset() is a no-op stub in the pico-sdk TinyUSB driver, so
 * enumeration never resets the port by itself.  No argument. */
#define PICOIOC_USBRESET 0x000D

/* Graphics (PC3: see PC3-GFX-DESIGN.md).  All on /dev/sys. */

/* data -> int: BBC mode 0-5, mode 7 (320x240 16 colours, NOT teletext),
 * or 0xFF back to the text console.  Modes 0-5 scan out at 1024x768
 * and the console and mode 7 at 640x480; switching within one of those
 * groups holds the monitor's lock, crossing between them does not. */
#define GFXIOC_MODE   0x0003

/* data -> int: (logical colour << 8) | physical colour.  Modes 0-5
 * take the authentic BBC 0-7; mode 7 takes all 16. */
#define GFXIOC_PAL    0x0004

/* data -> struct gfx_blit: copy bytes into the mode framebuffer.
 * Layout (PC3 modes): 4bpp high nibble = left pixel, 1bpp MSB = left;
 * line stride 160 bytes (modes 1/4/7) or 80 (modes 0/2/3/5), and 256
 * lines - except mode 7, which has 240. */
struct gfx_blit {
    uint16_t offset;            /* byte offset into the framebuffer */
    uint16_t len;
    void *buf;
};
#define GFXIOC_BLIT   0x0005

/* BBC sound (PC3): the SOUND statement's raw parameters; the channel
 * word carries the &1x flush and &Sxx sync bits.  Returns -1/EAGAIN
 * when that channel's note queue is full. */
struct snd_cmd {
    int16_t chan;
    int16_t amp;                /* -15..0 volume, 1-16 = ENVELOPE n */
    int16_t pitch;
    int16_t dur;                /* 20ths of a second, 255 = forever */
};
#define SNDIOC_SOUND  0x0006

/* data -> 14 bytes: N,T,PI1,PI2,PI3,PN1,PN2,PN3,AA,AD,AS,AR,ALA,ALD */
#define SNDIOC_ENV    0x0007

/* silence everything, flush all queues */
#define SNDIOC_QUIET  0x0008

/* BBC ADVAL (PC3): data -> int selector, returns the reading.
 *   0      joystick switches GP34-37 (pulled up, active low),
 *          pressed = 1: bit0 GP34, bit1 GP35, bit2 GP36, bit3 GP37
 *   1-4    ADC GP41-GP44, 16-bit (12-bit ADC << 4)
 *   -5..-8 sound channel 0-3 queue free slots
 *   -9     hardware microsecond counter, 31 bits in the return value
 *   -10    hardware microsecond counter, 64 bits: pass an 8-byte
 *          buffer whose low word holds the selector; the kernel
 *          writes the full value back into it and returns 0 */
#define PICOIOC_ADVAL 0x0009

/* The PSRAM arena (PC3-PSRAM-ARENA.md): a region of PSRAM outside the
 * process image.  Not context-switch copied, not swapped, not forked -
 * and not protected: the base address is raw, because with no MMU
 * there is nothing else it could be.  Released on exec and exit; a
 * fork leaves the arena with the parent. */
struct psram_req {
	uint32_t len;			/* in: bytes (4K granular) */
	uint32_t base;			/* out: address, raw */
};
struct psram_stat {
	uint32_t total;
	uint32_t free;
	uint32_t largest;
};
#define PSRAMIOC_ALLOC 0x000A		/* struct psram_req */
#define PSRAMIOC_FREE  0x000B		/* uint32_t base */
#define PSRAMIOC_STAT  0x000C		/* struct psram_stat */

#endif

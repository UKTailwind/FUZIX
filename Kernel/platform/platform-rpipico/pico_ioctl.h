#ifndef PICO_IOCTL_H
#define PICO_IOCTL_H

#include <stdint.h>

/* Reboot PI Pico into flash mode */
#define PICOIOC_FLASH 0x0001

/* Set the USB keyboard layout: data -> a 2-letter layout name
 * (US/UK/DE/FR/ES/BE, case-insensitive) */
#define PICOIOC_KBDMAP 0x0002

/* BBC graphics (PC3: see PC3-GFX-DESIGN.md).  All on /dev/sys. */

/* data -> int: BBC mode 0-5, or 0xFF back to the text console */
#define GFXIOC_MODE   0x0003

/* data -> int: (logical colour << 8) | BBC physical colour 0-15 */
#define GFXIOC_PAL    0x0004

/* data -> struct gfx_blit: copy bytes into the mode framebuffer.
 * Layout (PC3 modes): 4bpp high nibble = left pixel, 1bpp MSB = left;
 * line stride 160 bytes (modes 1/4) or 80 (modes 0/2/3/5), 256 lines. */
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

#endif

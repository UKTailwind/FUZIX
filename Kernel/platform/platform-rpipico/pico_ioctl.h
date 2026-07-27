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

#endif

#ifndef PC3_DISPLAY_PRIV_H
#define PC3_DISPLAY_PRIV_H
/*
 * The seam between display.c and display_hstx.c.
 *
 * display.c is the portable half: framebuffer ownership, mode and
 * palette state, and every drawing primitive, over plain byte buffers.
 * display_hstx.c is the PC3's scanout: rasters, HSTX, DMA, core1 and the
 * memory map.  This header is everything the two need to say to each
 * other, and it is deliberately small - the PC3 device server on a PC
 * compiles display.c against its own implementation of what is
 * declared here (build it with -DPC3_HOST), so anything added below is
 * a promise to two machines.
 *
 * Nothing outside the two files includes this.  display.h remains the
 * public interface.
 */

#include <stdint.h>
#include "display.h"

/* The expander the scanout runs, which is also how the core knows a
 * mode's width and depth.  Owned by display.c, read by core1. */
enum gexp {
    EXP_CONSOLE = 0,
    EXP_4BPP_X2,        /* mode 7:   320 wide, 160 bytes/line, 240 lines */
    EXP_4BPP_X3,        /* modes 1/4: 320 wide, 160 bytes/line */
    EXP_4BPP_X6,        /* modes 2/5: 160 wide, 80 bytes/line  */
    EXP_1BPP_5TO8,      /* modes 0/3: 640 wide -> 1024 (5:8), x3 lines */
};

extern volatile enum gexp gfx_exp;

/* The live palette in RGB332 - the byte core1 puts on the wire.  Owned
 * by display.c; the scanout's expansion table is rebuilt from it. */
extern uint8_t gfx_pal[16];

/* Which raster a mode lives on.  Only a change of raster costs the
 * monitor a resync; the core says which, the hardware knows the timing. */
#define DISP_RASTER_VGA 0       /* 640x480: the console and MODE 7 */
#define DISP_RASTER_XGA 1       /* 1024x768: BBC modes 0-5 */

/*
 * The mode switch, in the order display_gfx_mode() calls them:
 *   prepare   pick the raster; stop the scanout if it changes (returns
 *             1) or wait for the top of blanking if it does not (0)
 *   tables    build whatever the expander needs for the new palette
 *   handover  the core has just stored gfx_exp: make it, and every
 *             store before it, visible to the scanout, then switch the
 *             timing to the raster prepare chose - the barrier lives here
 *   finish    restart the scanout if prepare stopped it
 * And palette_changed: the live palette was rewritten in place.
 */
int  disp_hw_mode_prepare(int raster);
void disp_hw_mode_tables(enum gexp ex);
void disp_hw_mode_handover(enum gexp ex, int raster);
void disp_hw_mode_finish(int rebuild);
void disp_hw_palette_changed(enum gexp ex);

/*
 * A child of the framebuffer's owner draws where the owner draws (see
 * display_fb_enter).  The kernel reads the process table for that; a
 * host build supplies a function, since struct p_tab is its own.
 */
#ifdef PC3_HOST
struct p_tab *disp_who_parent(struct p_tab *who);
#else
#include <kernel.h>
#define disp_who_parent(who) ((who)->p_pptr)
#endif

#endif /* PC3_DISPLAY_PRIV_H */

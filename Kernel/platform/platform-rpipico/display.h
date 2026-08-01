#ifndef PC3_DISPLAY_H
#define PC3_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

/* Console: 640x480, 1 bit per pixel, MSB leftmost: 80 bytes per line */
#define DISP_WIDTH   640
#define DISP_HEIGHT  480
#define DISP_STRIDE  (DISP_WIDTH / 8)

/* Colour attributes per 8x12 character cell (RGB332 fg/bg): 80x40 cells */
#define DISP_CELL_W  8
#define DISP_CELL_H  12
#define DISP_COLS    (DISP_WIDTH / DISP_CELL_W)    /* 80 */
#define DISP_ROWS    (DISP_HEIGHT / DISP_CELL_H)   /* 40 */

/* The framebuffer pool serves the console (38400 bytes) and the
 * graphics modes (up to 40960: 320x256 at 4bpp; MODE 7 uses 38400,
 * 320x240 at 4bpp) - never both. */
#define DISP_FB_POOL 40960

extern uint8_t disp_fb[DISP_FB_POOL];
extern uint8_t disp_tile_fg[DISP_ROWS * DISP_COLS];
extern uint8_t disp_tile_bg[DISP_ROWS * DISP_COLS];

/* Start the scanout on core1 */
void display_init(void);

bool display_in_blanking(void);
void display_stack_check(void);
bool display_stack_ok(void);

/* Graphics modes (PC3-GFX-DESIGN.md), framebuffer at mode resolution
 * in disp_fb; 0xFF returns to the text console.  Returns fb size in
 * bytes, -1 on a mode we do not have.
 *   0-5  BBC modes, 1024x768 raster
 *   7    320x240, 16 colours, 640x480 raster (NOT teletext)
 * Switches within one raster keep the monitor locked; only 640x480
 * <-> 1024x768 restarts the scanout. */
int display_gfx_mode(int mode);
void display_gfx_pal(uint8_t logical, uint8_t physical);
int display_gfx_size(void);

#endif

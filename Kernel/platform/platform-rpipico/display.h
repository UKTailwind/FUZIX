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

/* The off-screen layer, in PSRAM (see display.c). Check display_fb2_ok()
 * before touching it: a board without PSRAM has nowhere to put it.
 *
 * This is MMBasic's FRAMEBUFFER "F", with the same draw-off-screen-then-
 * show shape: selecting it points the DRAWING primitives at it, scanout
 * is unaffected and always reads disp_fb, so the picture appears only on
 * the copy.
 *
 * Which buffer the primitives write to is a property of the CALLING
 * PROCESS, not of the machine: one process owns the layer at a time
 * (display_fb_open), and every graphics ioctl re-points the primitives
 * for whoever is making it (display_fb_enter). Without that, a program
 * that selected the layer and then blocked would have the next process
 * to draw - or the shell, or a repaint - land in its picture, and a
 * program that exited without deselecting would leave the machine
 * drawing off-screen with no way back.
 *
 * struct p_tab is Fuzix's process entry; display.h is included where
 * kernel.h is not, so it stays incomplete here. */
struct p_tab;
extern uint8_t disp_fb2[DISP_FB_POOL];
int display_fb2_ok(void);
/* claim (1) or give up (0) the layer.  0 ok, -1 no layer on this board,
 * -2 another process holds it. */
int display_fb_open(struct p_tab *who, int claim);
/* 0 = the screen, 1 = the layer.  -1 if the caller has not claimed it. */
int display_fb_select(struct p_tab *who, int which);
/* Point the primitives at the caller's own target.  Called once per
 * graphics ioctl; a process that owns nothing gets the screen. */
void display_fb_enter(struct p_tab *who);
/* Process gone (exit, exec) - drop any claim it held. */
void display_fb_release(struct p_tab *who);
/* to_layer 0: layer -> screen, 1: screen -> layer.  -1 unless the caller
 * holds the layer. */
int display_fb_copy(struct p_tab *who, int to_layer);
/* Where the drawing primitives are currently pointed. */
uint8_t *display_fb_target(void);
/* Block until the top of vertical blanking (bounded: if the scanout has
 * stopped this returns rather than hanging the caller). */
void display_wait_vblank(void);
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
int display_gfx_fbsize(void);

/* Drawing primitives, dispatched by the current mode - MMBasic's shape
 * (it swings a DrawPixel/DrawRectangle function pointer at each mode
 * change; a switch is the same thing without the indirect call).
 * Pixel is kept tight rather than routed through rectangle, as MMBasic
 * does, so it skips the swapping and clipping.
 * Out-of-range coordinates are dropped, not an error.  Returns -1 only
 * when there is no drawable mode. */
int display_gfx_pixel(int x, int y, int c);
int display_gfx_rect(int x1, int y1, int x2, int y2, int c);
void display_gfx_colour(uint32_t rgb888);
int display_gfx_curcol(void);
int display_gfx_getpixel(int x, int y);

/* RGB888 -> the live mode's own colour, without setting the current one.
 * Bitmap takes an explicit foreground and background, so it needs both
 * converted; bc < 0 means transparent, as MMBasic's -1 does. */
uint8_t display_gfx_map(uint32_t rgb888);

/* Batched: a whole shape in one call, so userland geometry pays the
 * syscall once instead of per point.  col NULL = the current colour;
 * otherwise one RGB888 per item, cached against the last value. */
struct gfx_pt;
struct gfx_rc;
int display_gfx_pixels(const struct gfx_pt *pt, int n, const uint32_t *col);
int display_gfx_rects(const struct gfx_rc *rc, int n, const uint32_t *col);
int display_gfx_bitmap(int x1, int y1, int width, int height, int scale,
                       int fc, int bc, const uint8_t *bitmap);

/* Current geometry, for GFXIOC_INFO. */
void display_gfx_geom(uint16_t *w, uint16_t *h, uint16_t *stride,
                      uint8_t *bpp, uint8_t *mode);

#endif

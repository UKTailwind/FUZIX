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

/*
 * And a THIRD buffer, the LAYER - MMBasic's FRAMEBUFFER LAYER.
 *
 * It is another off-screen framebuffer and nothing more; what makes it
 * a layer is MERGE, which composites it OVER the F buffer and puts the
 * result on the screen, skipping whichever colour index is nominated
 * transparent.  Neither source is changed.
 *
 * MMBasic implements overlays two ways.  Its VGA and HDMI builds test
 * three buffers per pixel inside the scanline builder, so the composite
 * is continuous and free; its TFT builds composite on demand in merge()
 * (FrameBuffer.c:844).  THIS PORT TAKES THE TFT MODEL, deliberately -
 * a scanout-time layer would have to live in SRAM for core1 to DMA it,
 * which is 40K off every process's 340K pool forever, for a feature
 * most programs never use.  PC3-LAYER-MERGE.md has the whole argument
 * and what would justify revisiting it (a mouse pointer).
 *
 * A program written for a PicoMite driving an ILI9341 therefore runs
 * unchanged; only the moment of compositing differs, and MMBasic itself
 * defines both moments.
 */
extern uint8_t disp_fb3[DISP_FB_POOL];
int display_fb2_ok(void);
int display_fb3_ok(void);

/* Which off-screen buffer: the argument to open, select and copy. */
#define DISP_FB_N	0		/* the screen, always present */
#define DISP_FB_F	1		/* FRAMEBUFFER CREATE */
#define DISP_FB_L	2		/* FRAMEBUFFER LAYER */

/* claim (1) or give up (0) one of the off-screen buffers.  0 ok, -1 no
 * PSRAM on this board, -2 another process holds them. */
int display_fb_open(struct p_tab *who, int claim, int which);
/* DISP_FB_N/F/L.  -1 if the caller has not created that buffer. */
int display_fb_select(struct p_tab *who, int which);
/* Point the primitives at the caller's own target.  Called once per
 * graphics ioctl; a process that owns nothing gets the screen. */
void display_fb_enter(struct p_tab *who);
/* Process gone (exit, exec) - drop any claim it held. */
void display_fb_release(struct p_tab *who);
/* Copy one whole buffer to another, any pair of DISP_FB_N/F/L.  -1
 * unless the caller holds the off-screen buffers it names. */
int display_fb_copy(struct p_tab *who, int src, int dst);
/* MERGE: L over F onto the screen, `colour` the transparent index
 * (0-15).  Neither source is modified.  -1 unless the caller holds
 * both. */
int display_fb_merge(struct p_tab *who, int colour);
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

/* MMBasic's MAP: an arbitrary RGB888 per palette entry, reduced to the
 * RGB332 the scanout emits.  remap() collects, apply() moves the whole
 * palette across during blanking, reset() restores the mode's own.
 * 16-colour modes only; -1 otherwise, as MMBasic refuses the rest. */
int display_gfx_remap(int index, uint32_t rgb888);
int display_gfx_remap_apply(void);
int display_gfx_remap_reset(void);

/* The fonts (fonts.c).  1-9 are MMBasic's built-in nine, in flash;
 * 10-16 are the CALLING PROCESS's own, registered with GFXIOC_FONTDEF
 * (MMBasic's DefineFont) and invisible to any other process.  NULL for
 * one that does not exist.  Every metric is read out of the font's own
 * four-byte header, so nothing anywhere carries a second idea of how
 * big a font is - which is what lets a user font of any cell size drop
 * in with no change to the renderer. */
const unsigned char *display_font(int font, int *w, int *h,
                                  int *first, int *count);
/* How many BUILT-IN fonts; the user slots are per process. */
int display_font_count(void);
/* Register one of the caller's own.  The caller must already have
 * checked the extent against the process (misc.c). */
int display_font_set(int font, const unsigned char *addr,
                     struct p_tab *owner);
/* Process gone (exit, exec) - drop its fonts.  Every process loads at
 * the same address, so a slot left behind would be a plausible pointer
 * into whatever runs next. */
void display_font_release(struct p_tab *who);

/* A run of text at a PIXEL position.  Draws through the caller's write
 * target like every other primitive, which is what lets PRINT reach the
 * off-screen buffer.  Returns the x the text ended at.
 *
 * font 1 is the console's own, so a program's text matches the shell's;
 * the cell is that font's width x height, times scale. */
#define GFX_TEXT_W 8                    /* font 1's cell - the console's */
#define GFX_TEXT_H 12
int display_gfx_text(int x, int y, int font, int scale, int fc, int bc,
                     const uint8_t *s, int len);
/* Scroll the drawing target: rows > 0 up, < 0 down, vacated band
 * filled with fillc.  THE one implementation - the console's graphics
 * scroll calls it and GFXIOC_SCROLL hands it to userland, so a PRINT
 * running off the bottom behaves the same whoever issued it. */
int display_gfx_scroll(int rows, int fillc);
/* both axes, wrap-capable - GFXIOC_SCROLL2; fillc is a native index,
 * or -1 leave, -2 wrap */
int display_gfx_scroll2(int dx, int dy, int fillc);

/* Current geometry, for GFXIOC_INFO. */
void display_gfx_geom(uint16_t *w, uint16_t *h, uint16_t *stride,
                      uint8_t *bpp, uint8_t *mode);

#endif

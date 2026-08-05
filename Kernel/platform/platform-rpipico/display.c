/*
 * Pico Computer 3 HDMI display for Fuzix.
 *
 * Two personalities share the scanout machinery (HSTX TMDS encode, sync
 * command lists, ping-pong scanline DMA, core1 line expansion):
 *
 * Two RASTERS, several modes on each.  The raster is all the monitor
 * sees, so only a change of raster costs a resync:
 *
 *  - 640x480 at clk_hstx = clk_sys/3 (375 MHz -> 25 MHz pixel, 59.5 Hz)
 *      console: 1bpp + RGB332 fg/bg per 8x12 cell (80x40)
 *      MODE 7:  320x240, 4bpp -> x2 h, x2 v, full screen
 *  - 1024x768 at clk_hstx = clk_sys, MMBasic's PC3-proven XGA line
 *    (375 MHz DDR -> 75 MHz pixel, 70.07 Hz)
 *      modes 1/4: 320x256, 4bpp -> x3 h, x3 v, 960 wide + 32px borders
 *      modes 2/5: 160x256, 4bpp -> x6 h, x3 v, ditto
 *      modes 0/3: 640x256, 1bpp -> 5:8 h (32-entry coverage LUT),
 *                 x3 v: full-screen 1024x768
 *
 * The framebuffer always stays at MODE resolution (20K/38K/40K, all
 * sharing the console's allocation) and core1 expands each output
 * scanline through a palette lookup table.  4bpp layout: high nibble =
 * left pixel.  1bpp: MSB = left.
 *
 * Core1 is owned by the display; nothing else may run there.  Switching
 * modes WITHIN one raster only swaps the expander, during vertical
 * blanking, with core1 still running - the monitor never loses lock.
 * Only a change of raster stops and relaunches core1.
 */

#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include "picosdk.h"
#include "config.h"
#include "display.h"
#include "psram.h"                      /* PSRAM_BASE, psram_size */
/* The fonts themselves live in fonts.c, reached through display_font;
 * this file no longer names font1 directly. */
#include <pico/platform/sections.h>     /* __uninitialized_psram */
/* struct gfx_pt / gfx_rc: the batched drawing items are part of the
 * userland interface, so their definition lives with the ioctls. */
#include "pico_ioctl.h"

#include <hardware/dma.h>
#include <hardware/resets.h>
#include <hardware/structs/hstx_ctrl.h>
#include <hardware/structs/hstx_fifo.h>
#include <hardware/structs/bus_ctrl.h>
#include <pico/multicore.h>

/* --- video timing -------------------------------------------------------- */
struct vtiming {
    uint16_t hfp, hsync, hbp, hact;
    uint16_t vfp, vsync, vbp, vact, vtotal;
    uint8_t  hstx_div;          /* clk_sys / this -> clk_hstx */
};

/* clk_sys is 375 MHz (MMBasic FreqXGA): console 640x480 runs at
 * clk_hstx = clk_sys/3 (25 MHz pixel, 59.5 Hz); the BBC graphics
 * modes use MMBasic's PC3-proven XGA line: full-rate HSTX (75 MHz
 * pixel), 1328x806 frame, 1024x768 at 70.07 Hz. */
static struct vtiming tim_vga = {
    16, 96, 48, 640,  10, 2, 33, 480, 525,  3
};
static struct vtiming tim_xga = {
    24, 136, 144, 1024,  3, 6, 29, 768, 806,  1
};
static struct vtiming *tim = &tim_vga;

/* --- HSTX command words and TMDS control symbols ------------------------ */
#define HSTX_CMD_RAW         (0x0u << 12)
#define HSTX_CMD_RAW_REPEAT  (0x1u << 12)
#define HSTX_CMD_TMDS        (0x2u << 12)
#define HSTX_CMD_NOP         (0xfu << 12)

#define TMDS_CTRL_00 (0x354u)
#define TMDS_CTRL_01 (0x0abu)
#define TMDS_CTRL_10 (0x154u)
#define TMDS_CTRL_11 (0x2abu)
#define SYNC_V0_H0 (TMDS_CTRL_00 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V0_H1 (TMDS_CTRL_01 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H0 (TMDS_CTRL_10 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H1 (TMDS_CTRL_11 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))

/* HSTX lane -> bit mapping for the PC3 (bit N -> GP12+N; positive on the
 * given odd bit, negative on bit-1). */
#define PC3_HDMI_CLK 1
#define PC3_HDMI_D0  3
#define PC3_HDMI_D1  5
#define PC3_HDMI_D2  7

/* --- Video memory -------------------------------------------------------- */
/* The pool serves the console (38400 used) and the BBC modes (up to
 * 40960): they never coexist. */
/* Aligned because the console expander reads it a WORD at a time
 * (MMBasic's HDMIloopX does the same). DISP_STRIDE is 80, a multiple of
 * four, so every row start is aligned once the base is. */
uint8_t disp_fb[DISP_FB_POOL] __attribute__((aligned(4)));

/*
 * A second framebuffer, in PSRAM.
 *
 * MMBasic's FRAMEBUFFER command draws into an off-screen layer and
 * blits it to the display, and the layer is firmware memory - the BASIC
 * program never sees it as a variable.  There is nowhere in SRAM to put
 * one: disp_fb is already 40,960 bytes and the kernel overran its
 * region by 864 bytes just from putting the console into graphics
 * modes.  Nor can it come out of the process image, where a translated
 * BASIC program has only 48K of VM space in total.
 *
 * So it is placed in the PSRAM window by the linker, using the SDK's
 * own mechanism (__uninitialized_psram, i.e. section
 * .psram_uninitialised.*), which is what that mechanism is for.  It
 * costs the disc 40K of swap and nothing else; psram_static_len() is
 * how devpsram.c knows to start the disc above it.
 *
 * DISP_FB_POOL rather than 38,400 so a layer exists for every mode the
 * pool serves, not just the one that prompted it.
 *
 * Drawing into PSRAM is a QMI transaction through a write-back XIP
 * cache, so it is slower than SRAM - which is exactly why MMBasic's
 * model is draw-then-COPY rather than scanning out from the layer.
 * Scanout must stay on disp_fb: core1 DMAs from it line by line.
 */
uint8_t disp_fb2[DISP_FB_POOL] __uninitialized_psram("fb2");

/* Present only if the window is really there and the link put the layer
 * inside it - a board with no PSRAM links the same but has nowhere for
 * it to live. */
int display_fb2_ok(void)
{
    uint32_t a = (uint32_t)disp_fb2;

    return psram_size &&
           a >= PSRAM_BASE && a + DISP_FB_POOL <= PSRAM_BASE + psram_size;
}

/*
 * Where the DRAWING primitives write.  Scanout is not switchable and
 * never looks at this: core1 always DMAs out of disp_fb, so pointing
 * this at the layer is exactly MMBasic's "FRAMEBUFFER WRITE F" - the
 * picture is built off-screen and appears on the COPY.
 *
 * It is DERIVED state, recomputed by display_fb_enter() at every
 * graphics ioctl from who is calling.  The truth is the two variables
 * below: which process holds the layer, and whether it is currently
 * drawing into it.  Anyone else - another program, the console's own
 * repaint - gets the screen, whatever the holder last asked for.
 */
static struct p_tab *fb_owner;          /* NULL = the layer is free */
static uint8_t fb_sel;                  /* owner is drawing into it */
static uint8_t *gfx_draw = disp_fb;

void display_fb_enter(struct p_tab *who)
{
    gfx_draw = (fb_sel && fb_owner == who) ? disp_fb2 : disp_fb;
}

uint8_t *display_fb_target(void)
{
    return gfx_draw;
}

/*
 * Claim or release the layer - MMBasic's FRAMEBUFFER CREATE and CLOSE F.
 * There is one layer, so a second claimant is told so (-2) rather than
 * quietly sharing a buffer with another program.  -1 means this board
 * has no PSRAM to put one in.
 */
int display_fb_open(struct p_tab *who, int claim)
{
    if (!claim) {
        display_fb_release(who);
        return 0;
    }
    if (!display_fb2_ok())
        return -1;
    if (fb_owner && fb_owner != who)
        return -2;
    fb_owner = who;
    return 0;
}

void display_fb_release(struct p_tab *who)
{
    if (fb_owner == who) {
        fb_owner = NULL;
        fb_sel = 0;
        gfx_draw = disp_fb;
    }
}

/* 0 = the screen, 1 = the layer.  Only its holder may select the layer;
 * anyone else gets -1 rather than silently drawing nowhere. */
int display_fb_select(struct p_tab *who, int which)
{
    if (which == 0) {
        if (fb_owner == who)
            fb_sel = 0;
        gfx_draw = disp_fb;
        return 0;
    }
    if (which != 1 || fb_owner != who)
        return -1;
    fb_sel = 1;
    gfx_draw = disp_fb2;
    return 0;
}

/* Blit between the layer and the screen - the whole live framebuffer,
 * which is stride*rows for a graphics mode and the console's own size
 * otherwise.  One memcpy: the layer holds the mode's own layout, so
 * there is no conversion.  MMBasic's FRAMEBUFFER COPY, whose source and
 * destination we can offer both ways round for the same memcpy. */
int display_fb_copy(struct p_tab *who, int to_layer)
{
    int n = display_gfx_fbsize();

    if (fb_owner != who || !display_fb2_ok())
        return -1;
    if (n <= 0 || n > DISP_FB_POOL)
        n = DISP_FB_POOL;
    if (to_layer)
        memcpy(disp_fb2, disp_fb, (unsigned)n);
    else
        memcpy(disp_fb, disp_fb2, (unsigned)n);
    return 0;
}
uint8_t disp_tile_fg[DISP_ROWS * DISP_COLS];
uint8_t disp_tile_bg[DISP_ROWS * DISP_COLS];

/* RGB332 expanded scanlines.  Word-aligned: the expanders write them
 * through uint32_t stores. */
/*
 * KEEP THIS IN MAIN SRAM, striped.  Moving it to SCRATCH_Y was tried
 * and was the worst result of the lot: short lines, and blue and green
 * flecking.  The striping is a feature - main SRAM is eight banks
 * interleaved by word, so a high-bandwidth stream spreads across all
 * eight and several masters proceed in parallel.  A scratch bank is ONE
 * bank: putting the DMA's 18.4 MB/s scanout read there serialised it
 * against core0's stack, which lives in the same bank, and starved the
 * HSTX FIFO outright.
 */
static uint8_t disp_lines[2][1024] __attribute__((aligned(4)));

static uint32_t vblank_line_vsync_off[7];
static uint32_t vblank_line_vsync_on[7];
static uint32_t vactive_line[9];

static volatile int32_t v_scanline = 2;
static volatile bool dma_pong = false;
static volatile bool vactive_cmdlist_posted = false;
static int dmach_ping = -1, dmach_pong = -1;

/* 512 bytes, the size MMBasic uses for the same HDMI scanout core.
 * Lives in SCRATCH_X, which the SDK reserves for a core1 stack we
 * never use (core1 is launched with this stack instead) - so it costs
 * no main SRAM, where the kernel and the 320K process area compete.
 * disp_core1_stack[0] holds a sentinel; disp_core1_healthy() checks it. */
#define CORE1_STACK_WORDS 128
#define STACK_SENTINEL    0xf00dbeefu
static uint32_t __scratch_x("disp") disp_core1_stack[CORE1_STACK_WORDS]
        __attribute__((aligned(8)));

/* --- BBC graphics state -------------------------------------------------- */
enum gexp {
    EXP_CONSOLE = 0,
    EXP_4BPP_X2,        /* mode 7:   320 wide, 160 bytes/line, 240 lines */
    EXP_4BPP_X3,        /* modes 1/4: 320 wide, 160 bytes/line */
    EXP_4BPP_X6,        /* modes 2/5: 160 wide, 80 bytes/line  */
    EXP_1BPP_5TO8,      /* modes 0/3: 640 wide -> 1024 (5:8), x3 lines */
};
static volatile enum gexp gfx_exp = EXP_CONSOLE;
static uint8_t gfx_pal[16];
/* MMBasic's remap332: MAP(n)=c collects here and MAP SET moves the lot
 * across in one go, during blanking.  The split is the whole point -
 * writing the live table entry by entry recolours the picture in
 * instalments, and a fade done that way is visibly wrong. */
static uint8_t gfx_pal_pending[16];
static uint16_t gfx_stride;                 /* source bytes per mode line */
static uint16_t gfx_rows;                   /* source lines: 256, or 240 */
static uint8_t gfx_mode_now = 0xFF;         /* the mode number as asked for */

/* One shared expansion LUT, 16-byte stride for shift-indexing:
 *  4BPP_X2: byte -> 4 output bytes;  4BPP_X3: 6;  4BPP_X6: 12;
 *  1BPP_5TO8: 8 (indexed by a 5-bit group, not a byte). */
static uint8_t gfx_lut[256][16] __attribute__((aligned(4)));

/* Physical colours in RGB332.  0-7 are the authentic BBC set, and are
 * all that modes 0-5 can reach (the real 8-15 flash, which we do not
 * do, so those map to their steady counterparts).  MODE 7 is our own
 * mode, not teletext, and uses all 16: 8-15 are a darker companion set
 * so its 16 logical colours are 16 DISTINCT colours by default. */
static uint8_t bbc_rgb332[16] = {
    0x00, 0xE0, 0x1C, 0xFC, 0x03, 0xE3, 0x1F, 0xFF,
    0x6D, 0x60, 0x0C, 0x6C, 0x01, 0x61, 0x0D, 0x24
};

/*
 * MODE 7 is what a translated MMBasic program draws in, and MMBasic's
 * own 4bpp screen is RGB121: one bit of red, two of green, one of
 * blue, so its sixteen colours are the corners of a regular cube.
 *
 * These are MMBasic's HDMI defaults exactly - RGB332(MAP16DEF[i]) from
 * HDMI.c, which is what mapreset() loads.  That is the right reference
 * and its VGA table is not: there the sixteen values only pick a slot,
 * because the colour itself is fixed by the output resistors.  Here, as
 * on MMBasic's HDMI, the byte IS the colour that goes on the wire.
 *
 * The four with mid green - 4, 5, 12 and 13 - are the ones that make
 * the difference: MAP16DEF uses 0x55 and 0xAA for the two middle green
 * levels, which truncate to 2 and 5, where a 0/64/128/255 ramp gives 2
 * and 4.  Those entries used to be a shade dark against the
 * interpreter on the same chip.
 */
static uint8_t rgb121_rgb332[16] = {
    0x00, 0x03, 0x08, 0x0B, 0x14, 0x17, 0x1C, 0x1F,
    0xE0, 0xE3, 0xE8, 0xEB, 0xF4, 0xF7, 0xFC, 0xFF
};

/* Mask applied to a physical colour number in this expander: MODE 7
 * reaches all 16, every BBC mode only the authentic 8. */
static uint8_t gfx_physmask(enum gexp ex)
{
    return (ex == EXP_4BPP_X2) ? 15 : 7;
}

static int gfx_bpp(enum gexp ex)
{
    return (ex == EXP_CONSOLE || ex == EXP_1BPP_5TO8) ? 1 : 4;
}

/* Built for the mode we are ABOUT to enter, so a live switch can have
 * the table ready before the expander is handed to core1. */
static void gfx_lut_rebuild(enum gexp ex)
{
    int b, i;
    switch (ex) {
    case EXP_4BPP_X2:
        for (b = 0; b < 256; b++) {
            uint8_t c1 = gfx_pal[b >> 4], c2 = gfx_pal[b & 15];
            uint8_t *e = gfx_lut[b];
            e[0] = e[1] = c1;
            e[2] = e[3] = c2;
        }
        break;
    case EXP_4BPP_X3:
        for (b = 0; b < 256; b++) {
            uint8_t c1 = gfx_pal[b >> 4], c2 = gfx_pal[b & 15];
            uint8_t *e = gfx_lut[b];
            e[0] = e[1] = e[2] = c1;
            e[3] = e[4] = e[5] = c2;
        }
        break;
    case EXP_4BPP_X6:
        for (b = 0; b < 256; b++) {
            uint8_t c1 = gfx_pal[b >> 4], c2 = gfx_pal[b & 15];
            uint8_t *e = gfx_lut[b];
            for (i = 0; i < 6; i++) {
                e[i] = c1;
                e[i + 6] = c2;
            }
        }
        break;
    case EXP_1BPP_5TO8: {
        /* 5 source pixels -> 8 output pixels: 32-entry LUT keyed by
         * the 5-bit source group.  The three fractional output pixels
         * per group blend the two source colours by linear coverage
         * (weights in fifths), anti-aliasing the fine 640-wide text.
         * Define GFX_MODE0_NEAREST for hard nearest-neighbour pixels
         * instead. */
        static uint8_t cov[8][3] = {
            /* {left src px, right src px, left weight /5} */
            { 0, 0, 5 }, { 0, 1, 3 }, { 1, 1, 5 }, { 1, 2, 1 },
            { 2, 3, 4 }, { 3, 3, 5 }, { 3, 4, 2 }, { 4, 4, 5 },
        };
        for (b = 0; b < 32; b++) {
            uint8_t c[5], *e = gfx_lut[b];
            for (i = 0; i < 5; i++)
                c[i] = (b & (0x10 >> i)) ? gfx_pal[1] : gfx_pal[0];
            for (i = 0; i < 8; i++) {
                uint8_t ca = c[cov[i][0]], cb = c[cov[i][1]];
#ifdef GFX_MODE0_NEAREST
                e[i] = (cov[i][2] >= 3) ? ca : cb;
#else
                uint8_t wa = cov[i][2], wb = 5 - wa;
                uint8_t r = ((ca >> 5) * wa + (cb >> 5) * wb) / 5;
                uint8_t g = (((ca >> 2) & 7) * wa + ((cb >> 2) & 7) * wb) / 5;
                uint8_t bl = ((ca & 3) * wa + (cb & 3) * wb) / 5;
                e[i] = (r << 5) | (g << 2) | bl;
#endif
            }
        }
        break;
    }
    default:
        break;
    }
}

/*
 * Everything the scanline interrupt needs, flattened out of the timing
 * struct once.  This is the hottest code in the machine: at 640x480 it
 * runs 525 lines x 60 Hz = 31,500 times a SECOND, on the core that must
 * not be late.
 *
 * It used to dereference tim five times per interrupt and finish with
 * "v_scanline = (v_scanline + 1) % tim->vtotal" - a hardware divide,
 * every scanline, to wrap a counter that only ever needs comparing
 * against a constant.  Both are gone: plain values, and a compare.
 *
 * Written only by disp_cache_timing(), which runs before core1 is
 * launched.  tim itself only changes on a "rebuild" mode switch, and
 * that path stops and restarts scanout around the change.
 */
static int t_vsync_start, t_vsync_end, t_blanking, t_hact_words, t_vtotal;

static void disp_cache_timing(void)
{
    t_vsync_start = tim->vfp;
    t_vsync_end   = tim->vfp + tim->vsync;
    t_blanking    = tim->vfp + tim->vsync + tim->vbp;
    t_hact_words  = tim->hact / 4;      /* 4 RGB332 pixels per word */
    t_vtotal      = tim->vtotal;
}

/* --- DMA IRQ (runs on core1): post the next scanline --------------------- */
static void __not_in_flash_func(disp_dma_irq)(void)
{
    uint ch_num = dma_pong ? dmach_pong : dmach_ping;
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    int line = v_scanline;

    dma_hw->ints1 = 1u << ch_num;
    dma_pong = !dma_pong;

    if (line >= t_vsync_start && line < t_vsync_end) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_on;
        ch->transfer_count = count_of(vblank_line_vsync_on);
    } else if (line < t_blanking) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_off;
        ch->transfer_count = count_of(vblank_line_vsync_off);
    } else if (!vactive_cmdlist_posted) {
        ch->read_addr = (uintptr_t)vactive_line;
        ch->transfer_count = count_of(vactive_line);
        vactive_cmdlist_posted = true;
        return;                         /* line does not advance here */
    } else {
        ch->read_addr = (uintptr_t)disp_lines[line & 1];
        ch->transfer_count = t_hact_words;
        vactive_cmdlist_posted = false;
    }

    /* Wrap by comparison, not by division. */
    if (++line >= t_vtotal)
        line = 0;
    v_scanline = line;
}

/* --- core1 fill loop: expand one scanline into RGB332 -------------------- */
static void __not_in_flash_func(disp_fill_loop)(void)
{
    /*
     * The timing is read ONCE, into registers, and never touched again.
     * This used to evaluate "tim->vtotal - tim->vact" and "tim->vact"
     * per scanline: a load of the tim pointer plus three loads through
     * it, four memory accesses every line, on the one path in this
     * system with a hard deadline.  Nothing here may cost more than it
     * has to - the margin is a single line, about 25us at 640x480, and
     * the expander below has to fill 640 bytes inside it.
     *
     * Safe because tim is invariant for the life of this loop: it only
     * changes when display_gfx_mode decides "rebuild", and that path
     * brackets the change with disp_scanout_stop/start, which is to say
     * it kills and relaunches core1 around it.
     *
     * gfx_exp is NOT hoisted, and must not be.  A mode change that
     * keeps the same video timing does not restart scanout - it waits
     * for vblank and hands over live, with gfx_exp as the handover
     * flag - so this loop has to keep re-reading it.
     */
    const int vblank = tim->vtotal - tim->vact;
    const int vact = tim->vact;
    int last_line = 2;
    for (;;) {
        if (v_scanline != last_line) {
            last_line = v_scanline;
            int active = last_line - vblank;
            if (active < 0 || active >= vact)
                continue;

            /*
             * NO __dmb() here, though MMBasic has one at the top of
             * every expander in HDMIloopX.  Tried, and it made things
             * WORSE - end-of-line artefacts in more colours - because
             * on this port core1 is time-limited, and a barrier that
             * drains outstanding transactions costs more than the
             * tearing it prevents.  MMBasic can afford it; we cannot,
             * yet.  Revisit if core1 ever gains real headroom.
             */
            uint8_t *p = disp_lines[last_line & 1];

            switch (gfx_exp) {
            case EXP_CONSOLE: {
                /*
                 * MMBasic's HDMIloopBTH640, SCREENMODE1 - its 640x480
                 * RGB332 tiled text expander, which is this mode
                 * exactly - copied as literally as the bit order
                 * allows.  Hundreds of hours went into these loops and
                 * some of the choices are empirical, so the shape is
                 * taken as given rather than re-derived:
                 *
                 *  - BYTE granular, one tile at a time.  A 32-pixel
                 *    word unroll was tried here and MMBasic tried it
                 *    too; MMBasic's own comment records dropping it,
                 *    because it hardcodes the width and assumes a row
                 *    stride divisible by four, which 720x400 (90 bytes
                 *    a row) is not.
                 *  - The tile colours are read INDEXED, fcol[i] and
                 *    bcol[i], not through incremented pointers, and not
                 *    hoisted into locals.  That is MMBasic's form and
                 *    it is faster here than the alternatives.
                 *  - One mask and a shift, rather than eight different
                 *    mask constants.
                 *
                 * The single deviation: MMBasic shifts RIGHT and tests
                 * bit 0, because its framebuffer is LSB-first within a
                 * byte.  Ours is MSB-first, so this shifts LEFT and
                 * tests bit 7 - the mirror image, same instruction
                 * count, our pixel order preserved.
                 */
                const uint8_t *fcol =
                    &disp_tile_fg[(active / DISP_CELL_H) * DISP_COLS];
                const uint8_t *bcol =
                    &disp_tile_bg[(active / DISP_CELL_H) * DISP_COLS];
                const uint8_t *dd = &disp_fb[active * DISP_STRIDE];

                for (int i = 0; i < DISP_COLS; i++) {
                    uint8_t d = dd[i];
                    *p++ = (d & 0x80) ? fcol[i] : bcol[i]; d <<= 1;
                    *p++ = (d & 0x80) ? fcol[i] : bcol[i]; d <<= 1;
                    *p++ = (d & 0x80) ? fcol[i] : bcol[i]; d <<= 1;
                    *p++ = (d & 0x80) ? fcol[i] : bcol[i]; d <<= 1;
                    *p++ = (d & 0x80) ? fcol[i] : bcol[i]; d <<= 1;
                    *p++ = (d & 0x80) ? fcol[i] : bcol[i]; d <<= 1;
                    *p++ = (d & 0x80) ? fcol[i] : bcol[i]; d <<= 1;
                    *p++ = (d & 0x80) ? fcol[i] : bcol[i];
                }
                break;
            }
            case EXP_4BPP_X2: {
                /* MODE 7: 320x240 4bpp doubled both ways inside the
                 * 640x480 console raster - 160 source bytes, one word
                 * of output each, exactly 640 pixels, no borders. */
                const uint8_t *s = &disp_fb[(active >> 1) * 160];
                for (int t = 0; t < 160; t++) {
                    *(uint32_t *)p = *(const uint32_t *)gfx_lut[s[t]];
                    p += 4;
                }
                break;
            }
            case EXP_4BPP_X3: {
                const uint8_t *s = &disp_fb[(active / 3) * 160];
                memset(p, 0, 32);
                p += 32;
                for (int t = 0; t < 160; t++) {
                    const uint8_t *e = gfx_lut[s[t]];
                    *(uint32_t *)p = *(const uint32_t *)e;
                    *(uint16_t *)(p + 4) = *(const uint16_t *)(e + 4);
                    p += 6;
                }
                memset(p, 0, 32);
                break;
            }
            case EXP_4BPP_X6: {
                const uint8_t *s = &disp_fb[(active / 3) * 80];
                memset(p, 0, 32);
                p += 32;
                for (int t = 0; t < 80; t++) {
                    const uint8_t *e = gfx_lut[s[t]];
                    *(uint32_t *)p = *(const uint32_t *)e;
                    *(uint32_t *)(p + 4) = *(const uint32_t *)(e + 4);
                    *(uint32_t *)(p + 8) = *(const uint32_t *)(e + 8);
                    p += 12;
                }
                memset(p, 0, 32);
                break;
            }
            case EXP_1BPP_5TO8: {
                /* full width: 640 source pixels -> 1024 out, 5:8.
                 * 5 source bytes = 40 bits = eight 5-bit groups. */
                const uint8_t *s = &disp_fb[(active / 3) * 80];
                for (int t = 0; t < 16; t++) {
                    uint32_t hi = ((uint32_t)s[0] << 16) |
                                  ((uint32_t)s[1] << 8) | s[2];
                    uint32_t lo = ((uint32_t)(s[2] & 0x0F) << 16) |
                                  ((uint32_t)s[3] << 8) | s[4];
                    const uint8_t *e;
                    e = gfx_lut[(hi >> 19) & 31];
                    *(uint32_t *)p = *(const uint32_t *)e;
                    *(uint32_t *)(p + 4) = *(const uint32_t *)(e + 4);
                    e = gfx_lut[(hi >> 14) & 31];
                    *(uint32_t *)(p + 8) = *(const uint32_t *)e;
                    *(uint32_t *)(p + 12) = *(const uint32_t *)(e + 4);
                    e = gfx_lut[(hi >> 9) & 31];
                    *(uint32_t *)(p + 16) = *(const uint32_t *)e;
                    *(uint32_t *)(p + 20) = *(const uint32_t *)(e + 4);
                    e = gfx_lut[(hi >> 4) & 31];
                    *(uint32_t *)(p + 24) = *(const uint32_t *)e;
                    *(uint32_t *)(p + 28) = *(const uint32_t *)(e + 4);
                    e = gfx_lut[(lo >> 15) & 31];
                    *(uint32_t *)(p + 32) = *(const uint32_t *)e;
                    *(uint32_t *)(p + 36) = *(const uint32_t *)(e + 4);
                    e = gfx_lut[(lo >> 10) & 31];
                    *(uint32_t *)(p + 40) = *(const uint32_t *)e;
                    *(uint32_t *)(p + 44) = *(const uint32_t *)(e + 4);
                    e = gfx_lut[(lo >> 5) & 31];
                    *(uint32_t *)(p + 48) = *(const uint32_t *)e;
                    *(uint32_t *)(p + 52) = *(const uint32_t *)(e + 4);
                    e = gfx_lut[lo & 31];
                    *(uint32_t *)(p + 56) = *(const uint32_t *)e;
                    *(uint32_t *)(p + 60) = *(const uint32_t *)(e + 4);
                    p += 64;
                    s += 5;
                }
                break;
            }
            }
        }
    }
}

/* --- core1 entry: clk_hstx, HSTX, DMA, IRQ, then the fill loop ----------- */
static void __not_in_flash_func(disp_core1_entry)(void)
{
    uint32_t hstx_in = clock_get_hz(clk_sys);
    uint32_t hstx_target = hstx_in / tim->hstx_div;
    clock_configure(clk_hstx, 0,
        CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
        hstx_in, hstx_target);

    vblank_line_vsync_off[0] = HSTX_CMD_RAW_REPEAT | tim->hfp;
    vblank_line_vsync_off[1] = SYNC_V1_H1;
    vblank_line_vsync_off[2] = HSTX_CMD_RAW_REPEAT | tim->hsync;
    vblank_line_vsync_off[3] = SYNC_V1_H0;
    vblank_line_vsync_off[4] = HSTX_CMD_RAW_REPEAT | (tim->hbp + tim->hact);
    vblank_line_vsync_off[5] = SYNC_V1_H1;
    vblank_line_vsync_off[6] = HSTX_CMD_NOP;

    vblank_line_vsync_on[0] = HSTX_CMD_RAW_REPEAT | tim->hfp;
    vblank_line_vsync_on[1] = SYNC_V0_H1;
    vblank_line_vsync_on[2] = HSTX_CMD_RAW_REPEAT | tim->hsync;
    vblank_line_vsync_on[3] = SYNC_V0_H0;
    vblank_line_vsync_on[4] = HSTX_CMD_RAW_REPEAT | (tim->hbp + tim->hact);
    vblank_line_vsync_on[5] = SYNC_V0_H1;
    vblank_line_vsync_on[6] = HSTX_CMD_NOP;

    vactive_line[0] = HSTX_CMD_RAW_REPEAT | tim->hfp;
    vactive_line[1] = SYNC_V1_H1;
    vactive_line[2] = HSTX_CMD_NOP;
    vactive_line[3] = HSTX_CMD_RAW_REPEAT | tim->hsync;
    vactive_line[4] = SYNC_V1_H0;
    vactive_line[5] = HSTX_CMD_NOP;
    vactive_line[6] = HSTX_CMD_RAW_REPEAT | tim->hbp;
    vactive_line[7] = SYNC_V1_H1;
    vactive_line[8] = HSTX_CMD_TMDS | tim->hact;

    /* RGB332 byte = RRRGGGBB. NBITS field is (bits - 1). */
    hstx_ctrl_hw->expand_tmds =
        0u << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB | 2u << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB |
        29u << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB | 2u << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
        26u << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB | 1u << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB;
    /* Four 8-bit pixels per 32-bit word. */
    hstx_ctrl_hw->expand_shift =
        4u << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
        8u << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
        1u << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
        0u << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;

    /* Serial: clock period 5 cycles, pop expander every 5, shift 2/cycle. */
    hstx_ctrl_hw->csr = 0;
    hstx_ctrl_hw->csr =
        HSTX_CTRL_CSR_EXPAND_EN_BITS |
        5u << HSTX_CTRL_CSR_CLKDIV_LSB |
        5u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
        2u << HSTX_CTRL_CSR_SHIFT_LSB |
        HSTX_CTRL_CSR_EN_BITS;

    /* Clock lane pair (base bit + inverted neighbour). */
    hstx_ctrl_hw->bit[PC3_HDMI_CLK] = HSTX_CTRL_BIT0_CLK_BITS;
    hstx_ctrl_hw->bit[PC3_HDMI_CLK - 1] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;

    static int lane_to_bit[3] = { PC3_HDMI_D0, PC3_HDMI_D1, PC3_HDMI_D2 };
    for (uint lane = 0; lane < 3; ++lane) {
        int bit = lane_to_bit[lane];
        uint32_t sel = (lane * 10) << HSTX_CTRL_BIT0_SEL_P_LSB |
            (lane * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB;
        hstx_ctrl_hw->bit[bit] = sel;
        hstx_ctrl_hw->bit[bit - 1] = sel | HSTX_CTRL_BIT0_INV_BITS;
    }

    for (int i = 12; i <= 19; ++i) {
        gpio_set_function(i, 0); /* HSTX */
        gpio_set_drive_strength(i, GPIO_DRIVE_STRENGTH_8MA);
        gpio_set_slew_rate(i, GPIO_SLEW_RATE_FAST);
        gpio_set_input_enabled(i, false);
        gpio_set_pulls(i, false, false);
        gpio_set_input_hysteresis_enabled(i, false);
    }

    /* Ping-pong DMA: each channel sends one scanline then chains on. */
    dma_channel_config c = dma_channel_get_default_config(dmach_ping);
    channel_config_set_chain_to(&c, dmach_pong);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(dmach_ping, &c, &hstx_fifo_hw->fifo,
        vblank_line_vsync_off, count_of(vblank_line_vsync_off), false);

    c = dma_channel_get_default_config(dmach_pong);
    channel_config_set_chain_to(&c, dmach_ping);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(dmach_pong, &c, &hstx_fifo_hw->fifo,
        vblank_line_vsync_off, count_of(vblank_line_vsync_off), false);

    dma_hw->ints1 = (1u << dmach_ping) | (1u << dmach_pong);
    dma_hw->inte1 = (1u << dmach_ping) | (1u << dmach_pong);
    irq_set_exclusive_handler(DMA_IRQ_1, disp_dma_irq);
    irq_set_enabled(DMA_IRQ_1, true); /* enabled on core1 -> ISR runs here */

    dma_channel_start(dmach_ping);

    disp_fill_loop();
}

/* --- scanout start/stop (mode switching) --------------------------------- */
static void disp_scanout_start(void)
{
    /*
     * CORE1 outranks everything on the bus.  It is the master with the
     * hard deadline, and the deadline is one scanline: disp_lines[] is
     * a pair, so core1 has to software-expand line N+1 while the DMA is
     * still sending line N.  At 640x480 that is about 25us, and the
     * console path alone writes 640 bytes through a bit-unpacking loop
     * to get there.  Delay core1 and the DMA ships a half-written
     * buffer - wrong pixels, while sync stays perfect, because sync
     * comes from a separate command list that never misses.  That is
     * exactly the flecking MP3 playback provoked, once core0 started
     * doing a memcpy out of PSRAM in an interrupt.
     *
     * bus_ctrl_hw->priority is left at the RESET DEFAULT, where every
     * master arbitrates equally - which is what MMBasic's HDMI path and
     * MicroPython both do.
     *
     * Three settings were tried while the display was flecking under
     * load, and none of them was the answer.  DMA_R alone (0x100, the
     * value MMBasic uses in its VGA and LCD branches but NOT under
     * "#ifdef HDMI") was markedly worse: it promotes every DMA master,
     * including the audio channels, above the one processor that must
     * not be late.  PROC1 alone was no better.  PROC1|DMA_R|DMA_W gave
     * fewer short lines but heavy flecking.
     *
     * The actual cause was elsewhere entirely - the MP3 decoder's
     * working set was in the PSRAM arena, and taking it out fixed the
     * display outright.  So nothing is set here: a register poke kept
     * only because it once seemed to help, against a problem since
     * removed, is worse than no poke at all.
     */
    disp_cache_timing();        /* before core1 can take an interrupt */
    v_scanline = 2;
    dma_pong = false;
    vactive_cmdlist_posted = false;
    disp_core1_stack[0] = STACK_SENTINEL;
    multicore_launch_core1_with_stack(disp_core1_entry,
        disp_core1_stack, sizeof(disp_core1_stack));
}

/* Proven teardown order (MMBasic HDMI.c via the PC3 MicroPython
 * driver): halt core1, break both DMA chains BEFORE aborting (point
 * chain_to at self through the non-triggering alias, else aborting
 * one re-triggers the other), abort both together through the abort
 * register with bounded waits, and only THEN stop HSTX.  Stopping
 * HSTX first kills the DREQ and leaves a channel BUSY forever - the
 * per-channel abort helper then spins and the caller hangs. */
static void disp_scanout_stop(void)
{
    uint32_t chans = (1u << dmach_ping) | (1u << dmach_pong);
    uint64_t dl;

    __sev();
    multicore_reset_core1();

    hw_write_masked(&dma_hw->ch[dmach_ping].al1_ctrl,
        (uint32_t)dmach_ping << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB,
        DMA_CH0_CTRL_TRIG_CHAIN_TO_BITS);
    hw_write_masked(&dma_hw->ch[dmach_pong].al1_ctrl,
        (uint32_t)dmach_pong << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB,
        DMA_CH0_CTRL_TRIG_CHAIN_TO_BITS);
    __dmb();
    dma_hw->abort = chans;
    dl = time_us_64() + 2000;
    while ((dma_hw->abort & chans) && time_us_64() < dl)
        tight_loop_contents();
    while ((dma_hw->ch[dmach_ping].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS) &&
           time_us_64() < dl)
        tight_loop_contents();
    while ((dma_hw->ch[dmach_pong].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS) &&
           time_us_64() < dl)
        tight_loop_contents();

    dma_hw->inte1 &= ~chans;
    dma_hw->ints1 = chans;
    hstx_ctrl_hw->csr = 0;
    irq_remove_handler(DMA_IRQ_1, disp_dma_irq);

    /* Fully reset the HSTX peripheral so the rebuild starts from
     * cold-boot state: reconfiguring it with residual FIFO/serialiser
     * state made MMBasic's live 640->1024 switches intermittently
     * fail to produce a valid signal.  The restart re-does all HSTX
     * and GPIO configuration, so this is safe. */
    reset_unreset_block_num_wait_blocking(RESETS_RESET_HSTX_LSB);
}

/* --- Public -------------------------------------------------------------- */
bool display_in_blanking(void)
{
    return v_scanline < (tim->vfp + tim->vsync + tim->vbp);
}

/* Checked on every mode switch: core1's stack is only 512 bytes, so
 * the sentinel is the guard that it is enough.  A breach is reported
 * once - silence here means the scanout core never came close. */
void display_stack_check(void)
{
    static uint8_t moaned;

    if (!moaned && disp_core1_stack[0] != STACK_SENTINEL) {
        moaned = 1;
        kprintf("display: core1 stack overflowed\n");
    }
}

bool display_stack_ok(void)
{
    return disp_core1_stack[0] == STACK_SENTINEL;
}

/* Wait for the top of vertical blanking, so a live switch has the
 * whole blanking interval to swap expander, palette and framebuffer
 * before the next active line is fetched.  Bounded, like MMBasic's
 * setmode(): if core1 ever stops, this must not hang the caller.
 * Blanking is ~670us at XGA / ~800us at VGA - far longer than the
 * ~40us the swap actually takes. */
static void gfx_wait_vblank(void)
{
    uint64_t dl = time_us_64() + 50000;

    while (v_scanline != 0 && time_us_64() < dl)
        tight_loop_contents();
}

/* The same wait, for userland: MMBasic's FRAMEBUFFER WAIT, and what
 * FRAMEBUFFER COPY ...,B does before it copies.  Line 0 exactly, which
 * is MMBasic's own "while(v_scanline!=0)": a caller gets the whole
 * blanking interval, and then the frame, before scanout reaches what it
 * is about to overwrite.  A 38K copy outlasts blanking either way, so
 * this reduces tearing rather than abolishing it. */
void display_wait_vblank(void)
{
    gfx_wait_vblank();
}

/* Enter a graphics mode - BBC 0-5, or MODE 7 (320x240, 16 colours) -
 * or 0xFF back to the text console.  Returns the framebuffer size, or
 * -1 for a mode we do not have.
 *
 * The scanout is only torn down and rebuilt when the underlying RASTER
 * changes, because that is the only thing the monitor can see: HSTX,
 * clk_hstx, the sync command lists and the DMA chain all belong to the
 * raster, not to the mode.  Modes 0-5 share 1024x768 and the console
 * and MODE 7 share 640x480, so every switch WITHIN either group is
 * done live during vertical blanking with core1 still running - the
 * monitor keeps lock and only the picture changes.  This is exactly
 * how MMBasic splits setmode() from restartHDMI(). */
int display_gfx_mode(int mode)
{
    extern void console_gfx(int active);    /* console.c */
    /* Default logical -> physical palettes, indexed by the pal column
     * below.  The BBC modes reach physical 0-7; MODE 7 reaches 16. */
    static const uint8_t defpal[4][16] = {
        /* 0: modes 0/3 - black, white */
        { 0, 7, 0, 7, 0, 7, 0, 7, 0, 7, 0, 7, 0, 7, 0, 7 },
        /* 1: modes 1/4 - black, red, yellow, white */
        { 0, 1, 3, 7, 0, 1, 3, 7, 0, 1, 3, 7, 0, 1, 3, 7 },
        /* 2: modes 2/5 - the full BBC set, twice */
        { 0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7 },
        /* 3: mode 7 - 16 distinct colours, BBC-authentic in the low 8 */
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
    };
    struct vtiming *newtim;
    enum gexp exp;
    uint16_t stride, rows;
    int pal, i;
    bool rebuild;

    display_stack_check();

    switch (mode) {
    case 0: case 3:
        exp = EXP_1BPP_5TO8; stride = 80;  rows = 256;
        newtim = &tim_xga; pal = 0;
        break;
    case 1: case 4:
        exp = EXP_4BPP_X3;   stride = 160; rows = 256;
        newtim = &tim_xga; pal = 1;
        break;
    case 2: case 5:
        exp = EXP_4BPP_X6;   stride = 80;  rows = 256;
        newtim = &tim_xga; pal = 2;
        break;
    case 7:
        exp = EXP_4BPP_X2;   stride = 160; rows = 240;
        newtim = &tim_vga; pal = 3;
        break;
    case 0xFF:
        exp = EXP_CONSOLE;   stride = 0;   rows = 0;
        newtim = &tim_vga; pal = -1;
        break;
    default:
        return -1;
    }

    /* The layer holds a picture in the OLD mode's geometry, and nothing
     * converts it - so it does not survive the change.  MMBasic's
     * setmode() opens with closeframebuffer('A') for the same reason;
     * this is that, and it is why a program creates its framebuffer
     * after choosing its mode, never before. */
    fb_owner = NULL;
    fb_sel = 0;
    gfx_draw = disp_fb;

    rebuild = (newtim != tim);

    if (rebuild)
        disp_scanout_stop();
    else
        gfx_wait_vblank();

    gfx_stride = stride;
    gfx_rows = rows;
    gfx_mode_now = (uint8_t)mode;

    if (exp == EXP_CONSOLE) {
        /* Blank the whole pool first: on a live switch core1 is still
         * scanning it out, and the old graphics picture would show for
         * a frame under the console expander before the repaint. */
        memset(disp_fb, 0, DISP_FB_POOL);
        gfx_exp = exp;
        __dmb();
        tim = newtim;
        console_gfx(0);         /* clears and repaints the console */
    } else {
        for (i = 0; i < 16; i++)
            gfx_pal[i] = gfx_pal_pending[i] = (exp == EXP_4BPP_X2)
                ? rgb121_rgb332[defpal[pal][i] & 15]
                : bbc_rgb332[defpal[pal][i] & gfx_physmask(exp)];
        /* Palette, table and framebuffer all ready BEFORE core1 is
         * told to use them - gfx_exp is the handover, and nothing the
         * expanders read may still be stale when it changes. */
        gfx_lut_rebuild(exp);
        memset(disp_fb, 0, (int)stride * rows);
        gfx_exp = exp;
        __dmb();
        tim = newtim;
        /* AFTER the handover, deliberately: the console now renders
         * into the graphics framebuffer rather than falling silent, so
         * it has to read the geometry of the mode being entered - and
         * display_gfx_geom() answers for gfx_exp. Called before this,
         * it sized the terminal from the mode we were leaving. */
        console_gfx(1);
    }

    if (rebuild)
        disp_scanout_start();
    return (int)stride * rows;
}

/* Set logical colour -> physical colour.  Modes 0-5 take the authentic
 * BBC 0-7 (8-15 flash on real hardware; here they map steady); MODE 7
 * takes all 16, from the RGB121 set it defaults to - so that physical
 * colour n means the same thing before and after a change. */
void display_gfx_pal(uint8_t logical, uint8_t physical)
{
    enum gexp ex = gfx_exp;

    gfx_pal[logical & 15] = gfx_pal_pending[logical & 15] =
        (ex == EXP_4BPP_X2)
        ? rgb121_rgb332[physical & 15]
        : bbc_rgb332[physical & gfx_physmask(ex)];
    gfx_lut_rebuild(ex);
}

/*
 * MMBasic's MAP - an arbitrary colour per palette entry, rather than
 * VDU19's choice from a fixed set.
 *
 * MAP(n) = colour collects into gfx_pal_pending and changes nothing;
 * MAP SET moves the whole palette across during blanking.  MMBasic
 * splits it the same way (remap332 -> map16quads on SET, after
 * `while (v_scanline != 0)`), and for the same reason: a fade or a
 * cycle applied entry by entry to the live table shows the picture
 * half recoloured.
 *
 * The stored value is RGB332 because that is what the scanout emits -
 * gfx_pal IS the byte core1 puts on the wire - so this is MMBasic's own
 * RGB332(): the top three bits of red and green and the top two of
 * blue, truncated, not rounded.
 *
 * 16-colour modes only, as MMBasic allows it only in SCREENMODE2/3/5.
 * The 1bpp modes have no palette to speak of and the console's tiles
 * are a different mechanism entirely.
 */
int display_gfx_remap(int index, uint32_t rgb888)
{
    if (gfx_bpp(gfx_exp) != 4 || index < 0 || index > 15)
        return -1;
    gfx_pal_pending[index] = (uint8_t)((rgb888 >> 16 & 0xE0) |
                                       (rgb888 >> 11 & 0x1C) |
                                       (rgb888 >> 6 & 0x03));
    return 0;
}

int display_gfx_remap_apply(void)
{
    int i;

    if (gfx_bpp(gfx_exp) != 4)
        return -1;
    /* Blanking first: the LUT rebuild writes the table core1 is reading
     * a scanline at a time, and doing that mid-frame tears the colours
     * across the picture. */
    display_wait_vblank();
    for (i = 0; i < 16; i++)
        gfx_pal[i] = gfx_pal_pending[i];
    gfx_lut_rebuild(gfx_exp);
    return 0;
}

/* MAP RESET - back to the mode's own defaults, live and pending alike,
 * which is what MMBasic's mapreset() does to map and remap together. */
int display_gfx_remap_reset(void)
{
    enum gexp ex = gfx_exp;
    int i;

    if (gfx_bpp(ex) != 4)
        return -1;
    display_wait_vblank();
    for (i = 0; i < 16; i++)
        gfx_pal[i] = gfx_pal_pending[i] = (ex == EXP_4BPP_X2)
            ? rgb121_rgb332[i]
            : bbc_rgb332[i & gfx_physmask(ex)];
    gfx_lut_rebuild(ex);
    return 0;
}

/* Current graphics framebuffer size (0 = console mode: the geometry is
 * zeroed on the way back, so this needs no special case). */
int display_gfx_size(void)
{
    return (int)gfx_stride * gfx_rows;
}

/* Size of the DRAWABLE framebuffer, which is not the same thing: the
 * console is drawable (640x480 1bpp = 38400 bytes) but its graphics
 * geometry is zeroed, so display_gfx_size() reports 0 for it.  BLIT
 * must use this, or it rejects every write to the text console. */
int display_gfx_fbsize(void)
{
    if (gfx_exp == EXP_CONSOLE)
        return DISP_STRIDE * DISP_HEIGHT;
    return (int)gfx_stride * gfx_rows;
}

/* --- drawing primitives -------------------------------------------------- */
/* Width and depth of the live mode.  The console is drawable too: it is
 * a 640x480 1bpp bitmap, with colour coming from the per-cell tiles. */
static int gfx_width(enum gexp ex)
{
    switch (ex) {
    case EXP_CONSOLE:    return DISP_WIDTH;     /* 640, 1bpp */
    case EXP_4BPP_X2:    return 320;
    case EXP_4BPP_X3:    return 320;
    case EXP_4BPP_X6:    return 160;
    case EXP_1BPP_5TO8:  return 640;
    }
    return 0;
}

void display_gfx_geom(uint16_t *w, uint16_t *h, uint16_t *stride,
                      uint8_t *bpp, uint8_t *mode)
{
    enum gexp ex = gfx_exp;

    *w = (uint16_t)gfx_width(ex);
    *bpp = (uint8_t)gfx_bpp(ex);
    if (ex == EXP_CONSOLE) {
        *h = DISP_HEIGHT;
        *stride = DISP_STRIDE;
        *mode = 0xFF;
    } else {
        *h = gfx_rows;
        *stride = gfx_stride;
        *mode = gfx_mode_now;
    }
}

/* The current drawing colour, already reduced to the mode's own
 * representation.  MMBasic's contract is that callers always speak
 * RGB888 and the primitive converts; doing it here, once per colour
 * change, keeps the per-pixel path down to a store. */
static uint8_t gfx_curcol = 1;

/* RGB888 -> whatever the live mode uses.  Split out from the current
 * colour because DrawBitmap takes an explicit foreground and background
 * and must convert both without disturbing it. */
uint8_t display_gfx_map(uint32_t rgb888)
{
    enum gexp ex = gfx_exp;
    uint8_t want, best = 0, i;
    int bestd = 0x7FFF;
    uint8_t r, g, b;

    if (gfx_bpp(ex) == 1) {
        /* Two colours: anything that is not black is ink.  This is what
         * MMBasic's DrawPixel2 does with its `if (c)`. */
        return rgb888 ? 1 : 0;
    }

    /*
     * MODE 7 is MMBasic's MODE 2, and there the index for a colour is
     * FIXED: RGB121() in the interpreter is pure bit extraction, taking
     * no notice of the palette.  That is what makes MAP work - remap an
     * entry and everything already drawn in it changes colour, while a
     * program carries on naming colours the same way.
     *
     * Nearest-match cannot do that.  Once MAP has moved an entry, the
     * nearest entry to red is no longer the one called red, so new
     * drawing lands somewhere else and a palette cycle takes the
     * picture apart.  The two agree exactly while the palette is the
     * default RGB121 cube, so this costs nothing until MAP is used -
     * and then it is the difference between working and not.
     */
    if (ex == EXP_4BPP_X2)
        return (uint8_t)(((rgb888 & 0x800000) >> 20) |
                         ((rgb888 & 0x00C000) >> 13) |
                         ((rgb888 & 0x000080) >> 7));

    /* The BBC modes keep nearest-match: their palette is a choice from
     * a fixed set of physical colours (VDU19), there is no encoding to
     * extract, and the set is not a regular cube. */
    r = (rgb888 >> 16) & 0xFF;
    g = (rgb888 >> 8) & 0xFF;
    b = rgb888 & 0xFF;
    want = ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6);
    for (i = 0; i < 16; i++) {
        int dr = ((gfx_pal[i] >> 5) & 7) - ((want >> 5) & 7);
        int dg = ((gfx_pal[i] >> 2) & 7) - ((want >> 2) & 7);
        int db = (gfx_pal[i] & 3) - (want & 3);
        int d = dr * dr + dg * dg + db * db * 4;  /* blue has 2 bits */
        if (d < bestd) {
            bestd = d;
            best = i;
        }
    }
    return best;
}

void display_gfx_colour(uint32_t rgb888)
{
    gfx_curcol = display_gfx_map(rgb888);
}

int display_gfx_curcol(void)
{
    return gfx_curcol;
}

/* RGB332 -> RGB888, so callers only ever see MMBasic's colour space. */
static uint32_t rgb332_to_888(uint8_t c)
{
    uint32_t r = ((c >> 5) & 7) * 255u / 7u;
    uint32_t g = ((c >> 2) & 7) * 255u / 7u;
    uint32_t b = (c & 3) * 255u / 3u;

    return (r << 16) | (g << 8) | b;
}

/* Read one pixel back AS RGB888 - MMBasic's PIXEL() function returns a
 * colour, not an index, and the conversion belongs in the primitive
 * for the same reason the forward one does.  -1 off-screen. */
int display_gfx_getpixel(int x, int y)
{
    enum gexp ex = gfx_exp;
    int w = gfx_width(ex);
    int h = (ex == EXP_CONSOLE) ? DISP_HEIGHT : gfx_rows;
    int stride = (ex == EXP_CONSOLE) ? DISP_STRIDE : gfx_stride;
    uint8_t v;

    if (!w || x < 0 || y < 0 || x >= w || y >= h)
        return -1;
    if (gfx_bpp(ex) == 4) {
        v = gfx_draw[y * stride + (x >> 1)];
        return (int)rgb332_to_888(gfx_pal[(x & 1) ? (v & 15) : (v >> 4)]);
    }
    /* 1bpp: the bit chooses ink or paper, and the actual colour lives
     * in the cell's tile attributes - so read those rather than
     * pretending the console is black and white. */
    v = gfx_draw[y * stride + (x >> 3)];
    if (ex == EXP_CONSOLE) {
        int cell = (y / DISP_CELL_H) * DISP_COLS + (x / DISP_CELL_W);
        uint8_t c = ((v >> (7 - (x & 7))) & 1) ? disp_tile_fg[cell]
                                               : disp_tile_bg[cell];
        return (int)rgb332_to_888(c);
    }
    return ((v >> (7 - (x & 7))) & 1) ? 0xFFFFFF : 0x000000;
}

/* One pixel.  Kept tight - no swapping, no clipping loop - because this
 * is the hot path: MMBasic's PIXEL statement costs 5us and a compiled
 * one has to beat it.  4bpp layout is high nibble = LEFT pixel, which
 * is framebuf's GS4_HMSB and the opposite of MMBasic's RGB121. */
int display_gfx_pixel(int x, int y, int c)
{
    enum gexp ex = gfx_exp;
    int w = gfx_width(ex);
    int h = (ex == EXP_CONSOLE) ? DISP_HEIGHT : gfx_rows;
    int stride = (ex == EXP_CONSOLE) ? DISP_STRIDE : gfx_stride;
    uint8_t *p;

    if (!w)
        return -1;
    if (x < 0 || y < 0 || x >= w || y >= h)
        return 0;               /* off-screen is not an error */

    if (gfx_bpp(ex) == 4) {
        p = &gfx_draw[y * stride + (x >> 1)];
        if (x & 1)
            *p = (*p & 0xF0) | (c & 15);        /* odd  -> low nibble */
        else
            *p = (*p & 0x0F) | ((c & 15) << 4); /* even -> high nibble */
    } else {
        p = &gfx_draw[y * stride + (x >> 3)];
        if (c)
            *p |= 0x80 >> (x & 7);              /* MSB = leftmost */
        else
            *p &= ~(0x80 >> (x & 7));
    }
    return 0;
}

/* A filled rectangle, which is also how lines arrive (x1==x2 or
 * y1==y2).  Ordering and clipping are done once, then the span loop is
 * flat - the point of having this as a primitive rather than making
 * userland call the pixel one in a loop. */
int display_gfx_rect(int x1, int y1, int x2, int y2, int c)
{
    enum gexp ex = gfx_exp;
    int w = gfx_width(ex);
    int h = (ex == EXP_CONSOLE) ? DISP_HEIGHT : gfx_rows;
    int stride = (ex == EXP_CONSOLE) ? DISP_STRIDE : gfx_stride;
    int x, y, t, xe;

    if (!w)
        return -1;
    if (x2 < x1) { t = x1; x1 = x2; x2 = t; }
    if (y2 < y1) { t = y1; y1 = y2; y2 = t; }
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= w) x2 = w - 1;
    if (y2 >= h) y2 = h - 1;
    if (x1 > x2 || y1 > y2)
        return 0;               /* entirely off-screen */

    if (gfx_bpp(ex) == 4) {
        uint8_t both = ((c & 15) << 4) | (c & 15);
        for (y = y1; y <= y2; y++) {
            uint8_t *row = &gfx_draw[y * stride];
            x = x1;
            if (x & 1) {        /* odd left edge: low nibble only */
                row[x >> 1] = (row[x >> 1] & 0xF0) | (c & 15);
                x++;
            }
            xe = x2;
            if ((x2 & 1) == 0 && x <= x2) {
                /* even right edge: high nibble only */
                row[x2 >> 1] = (row[x2 >> 1] & 0x0F) | ((c & 15) << 4);
                xe = x2 - 1;
            }
            /* What is left starts on a byte and ends on one, so it is
             * a memset - which is the point.  A span was being filled
             * a byte at a time, half the speed of the interpreter's
             * DrawRectangle16 doing exactly this, and filling is most
             * of the work in anything that draws solid shapes. */
            if (x <= xe)
                memset(&row[x >> 1], both, (unsigned)(((xe - x) >> 1) + 1));
        }
    } else {
        for (y = y1; y <= y2; y++) {
            uint8_t *row = &gfx_draw[y * stride];
            for (x = x1; x <= x2; x++) {
                if (c)
                    row[x >> 3] |= 0x80 >> (x & 7);
                else
                    row[x >> 3] &= ~(0x80 >> (x & 7));
            }
        }
    }
    return 0;
}

/* A run of points in one call.  This is the batched form the geometry
 * in userland draws through, so the setup - which mode, how wide, how
 * deep - is hoisted out of the loop and paid once for the whole shape,
 * where display_gfx_pixel pays it per call.
 *
 * col may be NULL for "all in the current colour".  When it is not,
 * the map from RGB888 to the mode's own colour is cached against the
 * last value: a constant-colour run maps once, and 4bpp mapping is a
 * sixteen-way nearest-match that would otherwise dominate.
 */
int display_gfx_pixels(const struct gfx_pt *pt, int n, const uint32_t *col)
{
    enum gexp ex = gfx_exp;
    int w = gfx_width(ex);
    int h = (ex == EXP_CONSOLE) ? DISP_HEIGHT : gfx_rows;
    int stride = (ex == EXP_CONSOLE) ? DISP_STRIDE : gfx_stride;
    int four = (gfx_bpp(ex) == 4);
    uint32_t last = 0;
    int c = gfx_curcol;
    int i, x, y;

    if (!w)
        return -1;
    if (col && n > 0) {
        last = col[0];
        c = display_gfx_map(last);
    }

    for (i = 0; i < n; i++) {
        if (col && col[i] != last) {
            last = col[i];
            c = display_gfx_map(last);
        }
        x = pt[i].x;
        y = pt[i].y;
        if (x < 0 || y < 0 || x >= w || y >= h)
            continue;               /* off-screen is dropped, not an error */
        if (four) {
            uint8_t *p = &gfx_draw[y * stride + (x >> 1)];
            if (x & 1)
                *p = (*p & 0xF0) | (c & 15);
            else
                *p = (*p & 0x0F) | ((c & 15) << 4);
        } else {
            uint8_t *p = &gfx_draw[y * stride + (x >> 3)];
            if (c)
                *p |= 0x80 >> (x & 7);
            else
                *p &= ~(0x80 >> (x & 7));
        }
    }
    return 0;
}

/* A run of rectangles - the other half of the batched pair, and what a
 * filled circle or polygon turns into: one span per scan line.  The
 * span loop dominates, so this reuses display_gfx_rect rather than
 * hoisting anything, which keeps it to a few dozen bytes of kernel. */
int display_gfx_rects(const struct gfx_rc *rc, int n, const uint32_t *col)
{
    uint32_t last = 0;
    int c = gfx_curcol;
    int i, r;

    if (!gfx_width(gfx_exp))
        return -1;
    if (col && n > 0) {
        last = col[0];
        c = display_gfx_map(last);
    }
    for (i = 0; i < n; i++) {
        if (col && col[i] != last) {
            last = col[i];
            c = display_gfx_map(last);
        }
        r = display_gfx_rect(rc[i].x1, rc[i].y1, rc[i].x2, rc[i].y2, c);
        if (r < 0)
            return r;
    }
    return 0;
}

/* A scaled 1-bit source bitmap - MMBasic's DrawBitmap2 and DrawBitmap16
 * folded into one, because at this level they differ only in how a pixel
 * is stored.  Every character on the screen goes through this, which is
 * why the editor needs it before anything else.
 *
 * fc and bc are already reduced to the mode's own colours; bc < 0 leaves
 * the background alone, which is MMBasic's `bc == -1` transparency.
 *
 * TWO conventions differ from MMBasic and both are deliberate:
 *
 *   - the SOURCE bit order is MMBasic's, verbatim, so its fonts and
 *     BLIT data can be used unchanged.  It looks strange - the shift is
 *     taken from the END of the bitmap - but for any bitmap whose total
 *     bit count is a multiple of 8 (every font) it is plain MSB-first.
 *   - the DESTINATION is ours: 1bpp MSB = leftmost pixel and 4bpp high
 *     nibble = left pixel, where MMBasic uses `1 << (x % 8)` and the
 *     low nibble.  See PC3-GFX-DESIGN.md - the port is unified with
 *     MicroPython's framebuf so assets interchange between the two PC3
 *     environments, and the scanout expander is indifferent.
 */
int display_gfx_bitmap(int x1, int y1, int width, int height, int scale,
                       int fc, int bc, const uint8_t *bitmap)
{
    enum gexp ex = gfx_exp;
    int w = gfx_width(ex);
    int h = (ex == EXP_CONSOLE) ? DISP_HEIGHT : gfx_rows;
    int stride = (ex == EXP_CONSOLE) ? DISP_STRIDE : gfx_stride;
    int bpp = gfx_bpp(ex);
    int nbits = width * height;
    int i, j, k, m, x, y, c;

    if (!w)
        return -1;
    if (width <= 0 || height <= 0 || scale <= 0)
        return -1;
    /* wholly off-screen: MMBasic's own early out, before any work */
    if (x1 >= w || y1 >= h ||
        x1 + width * scale < 0 || y1 + height * scale < 0)
        return 0;

    for (i = 0; i < height; i++) {              /* source scan line */
        for (j = 0; j < scale; j++) {           /* repeated to scale */
            y = y1 + i * scale + j;
            if (y < 0 || y >= h)
                continue;
            for (k = 0; k < width; k++) {       /* bit in that line */
                int n = i * width + k;
                int set = (bitmap[n >> 3] >> ((nbits - n - 1) & 7)) & 1;

                c = set ? fc : bc;
                if (c < 0)
                    continue;                   /* transparent paper */
                for (m = 0; m < scale; m++) {
                    uint8_t *p;

                    x = x1 + k * scale + m;
                    if (x < 0 || x >= w)
                        continue;
                    if (bpp == 4) {
                        p = &gfx_draw[y * stride + (x >> 1)];
                        if (x & 1)
                            *p = (*p & 0xF0) | (c & 15);
                        else
                            *p = (*p & 0x0F) | ((c & 15) << 4);
                    } else {
                        p = &gfx_draw[y * stride + (x >> 3)];
                        if (c)
                            *p |= 0x80 >> (x & 7);
                        else
                            *p &= ~(0x80 >> (x & 7));
                    }
                }
            }
        }
    }
    return 0;
}

/*
 * A run of text at a PIXEL position - MMBasic's GUIPrintChar, which is
 * how PRINT reaches the screen in a graphics mode.
 *
 * Any of the built-in fonts (fonts.c), font 1 being the console's own -
 * MMBasic's font1 - so a program's text matches the shell's.  The
 * layout is MMBasic's throughout: header [width][height][first][count]
 * then the glyphs, each width*height bits packed continuously.
 *
 * It goes through display_gfx_bitmap, so it writes to gfx_draw like
 * every other primitive - which is the whole point.  A program that has
 * selected the off-screen buffer gets its text there, instead of the
 * console scribbling on the screen underneath the picture.
 *
 * One call for the whole string rather than one per character: a
 * counter redrawn every frame is the case this exists for.
 */
int display_gfx_text(int x, int y, int font, int scale, int fc, int bc,
                     const uint8_t *s, int len)
{
    int i, w, h, first, count, glyph;
    const uint8_t *fp = display_font(font, &w, &h, &first, &count);

    if (!fp)
        return -1;
    if (scale <= 0)
        scale = 1;
    /* Bytes per glyph.  NOT h: that only holds for a font 8 pixels
     * wide, and of the nine only two are. */
    glyph = (w * h) / 8;

    for (i = 0; i < len; i++) {
        int c = s[i];

        if (c < first || c >= first + count) {
            /* Not in the font.  MMBasic fills the cell with the paper
             * colour and moves on - which for font 6, whose 11 glyphs
             * are the digits, is every other character.  Substituting a
             * space would index off the end of that font. */
            if (bc >= 0)
                display_gfx_rect(x, y, x + w * scale - 1,
                                 y + h * scale - 1, bc);
        } else {
            display_gfx_bitmap(x, y, w, h, scale, fc, bc,
                               &fp[4 + (c - first) * glyph]);
        }
        x += w * scale;
    }
    return x;
}

/*
 * Scroll the drawing target - the ONE implementation.
 *
 * rows > 0 moves the picture up, rows < 0 down, and the vacated band is
 * filled with fillc.  The console calls this for its graphics modes and
 * userland reaches it through GFXIOC_SCROLL, so a PRINT running off the
 * bottom does the same thing whoever issued it.
 *
 * It moves gfx_draw, not disp_fb, which is the point: con_gfx_scroll
 * used to memmove the SCREEN while con_gfx_plot and con_gfx_clear drew
 * through the write target, so a console that scrolled while a program
 * was drawing off-screen moved the wrong picture.  Nothing had noticed
 * because nothing had yet printed with a framebuffer selected.
 */
int display_gfx_scroll(int rows, int fillc)
{
    enum gexp ex = gfx_exp;
    int h = (ex == EXP_CONSOLE) ? DISP_HEIGHT : gfx_rows;
    int stride = (ex == EXP_CONSOLE) ? DISP_STRIDE : gfx_stride;
    int n = rows < 0 ? -rows : rows;
    int keep;
    uint8_t fill;

    if (!stride || !rows)
        return -1;
    /* both nibbles, or all eight bits, of the incoming band */
    fill = (gfx_bpp(ex) == 4) ? (uint8_t)((fillc & 15) | ((fillc & 15) << 4))
                              : (uint8_t)(fillc ? 0xFF : 0);
    if (n >= h) {
        memset(gfx_draw, fill, (unsigned)(stride * h));
        return 0;
    }
    keep = (h - n) * stride;
    if (rows > 0) {
        memmove(gfx_draw, gfx_draw + n * stride, (unsigned)keep);
        memset(gfx_draw + keep, fill, (unsigned)(n * stride));
    } else {
        memmove(gfx_draw + n * stride, gfx_draw, (unsigned)keep);
        memset(gfx_draw, fill, (unsigned)(n * stride));
    }
    return 0;
}

void display_init(void)
{
    dmach_ping = dma_claim_unused_channel(true);
    dmach_pong = dma_claim_unused_channel(true);

    disp_scanout_start();

    kputs("HDMI display: 640x480 console, graphics modes 0-5 and 7\n");
}

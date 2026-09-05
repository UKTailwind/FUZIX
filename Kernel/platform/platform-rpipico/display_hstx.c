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

/*
 * THIS IS THE HARDWARE HALF.  The drawing primitives, the framebuffer
 * ownership and the mode and palette state moved to display.c, which is
 * portable and shared with the PC3 device server; what stays here is
 * everything that names a register, a core or an address: the rasters,
 * the HSTX and DMA programming, core1's fill loop, where the three
 * framebuffers live, and the handful of hooks display_priv.h declares.
 * display.h is the unchanged public interface to both halves.
 */

#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include "picosdk.h"
#include "config.h"
#include "display.h"
#include "display_priv.h"
#include "psram.h"                      /* PSRAM_BASE, psram_size */
#include <pico/platform/sections.h>     /* __uninitialized_psram */

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

/*
 * The LAYER, beside it and in the same window.  Another 40K of PSRAM
 * and of swap, which against 8 MB is not a consideration - the reason
 * a layer was not built before is that a SCANOUT-time one would have
 * had to be in SRAM, and that is 12% of every process's memory
 * permanently.  See display.h and PC3-LAYER-MERGE.md.
 */
uint8_t disp_fb3[DISP_FB_POOL] __uninitialized_psram("fb3");

/* Present only if the window is really there and the link put the layer
 * inside it - a board with no PSRAM links the same but has nowhere for
 * it to live. */
static int fb_in_psram(const uint8_t *p)
{
    uint32_t a = (uint32_t)p;

    return psram_size &&
           a >= PSRAM_BASE && a + DISP_FB_POOL <= PSRAM_BASE + psram_size;
}

int display_fb2_ok(void)
{
    return fb_in_psram(disp_fb2);
}

int display_fb3_ok(void)
{
    return fb_in_psram(disp_fb3);
}

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

/* One shared expansion LUT, 16-byte stride for shift-indexing:
 *  4BPP_X2: byte -> 4 output bytes;  4BPP_X3: 6;  4BPP_X6: 12;
 *  1BPP_5TO8: 8 (indexed by a 5-bit group, not a byte). */
static uint8_t gfx_lut[256][16] __attribute__((aligned(4)));

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
/*
 * The one watchdog that survives core0 dying with interrupts off.
 *
 * The tick holds di() across its whole body, so if it hangs in there
 * interrupts never come back and every diagnostic on core0 dies with
 * it: no fault so no dump, no tick so no kernel watchdog, no console
 * because the shell is core0's too.  The machine looks dead and says
 * nothing - which is the picofrog freeze exactly.
 *
 * core1 is untouched by any of that.  It paints out of the framebuffer
 * and takes none of core0's interrupts or locks, so it can watch the
 * tick counter and report when it stops - and, from the phase
 * breadcrumb, WHICH CALL in the tick it stopped in.  The report goes
 * straight at the uart's data register: the transmit ring, the tty
 * layer and the console are all core0's and all suspect.  Hardware
 * uart1 is the console here (config.h sets DEV_UART_0_INSTANCE to 1).
 *
 * Phases: 1 usbkbd_tick, 2 tty_interrupt, 3 rawuart_tx_poll,
 *         4 timer_interrupt, 5 pre-empt/re-arm (tick body finished).
 */
extern volatile uint32_t pc3_tickbeat, pc3_syscount;
extern volatile uint8_t pc3_tickphase;

static uint32_t c1_beat, c1_frames;

static void __not_in_flash_func(c1_putc)(char c)
{
    uart_hw_t *hw = uart_get_hw(uart1);

    while (hw->fr & UART_UARTFR_TXFF_BITS)
	;
    hw->dr = (uint8_t)c;
}

static void __not_in_flash_func(c1_str)(const char *s)
{
    while (*s)
	c1_putc(*s++);
}

static void __not_in_flash_func(c1_hex)(uint32_t v, int n)
{
    static const char d[] = "0123456789ABCDEF";

    while (--n >= 0)
	c1_putc(d[(v >> (n * 4)) & 15]);
}

/*
 * One compare a frame, and it KEEPS saying it - every two seconds for
 * as long as the tick is stopped.  Reporting once was a race that lost
 * a run: the line goes out the instant the machine dies, and whoever is
 * watching the port has usually not attached yet, or resets the board
 * before reading it.  A frozen machine has nothing else to say, so
 * repeating costs nothing and means the evidence is there whenever
 * someone looks.
 */
static void __not_in_flash_func(disp_core1_watch)(void)
{
    if (pc3_tickbeat != c1_beat) {
	c1_beat = pc3_tickbeat;
	c1_frames = 0;
	return;
    }
    if (++c1_frames < 120)			/* ~2s at 60Hz */
	return;
    c1_frames = 0;

    /*	nsys is the one that matters now.  Watch it ACROSS the repeats:
     *	climbing means core0 is alive in userland and only the timer
     *	interrupt has stopped; frozen means core0 is not executing at
     *	all, and then the dead tick is a symptom rather than the fault. */
    c1_str("\r\n[CORE0 STALLED phase=");
    c1_hex(pc3_tickphase, 1);
    c1_str(" beat=");
    c1_hex(pc3_tickbeat, 8);
    c1_str(" nsys=");
    c1_hex(pc3_syscount, 8);
    c1_str("]\r\n");
}

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
            if (active < 0 || active >= vact) {
                /* Once a frame, in vblank, where there is time. */
                if (last_line == 0)
                    disp_core1_watch();
                continue;
            }

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

/*
 * The same wait in SLICES, for a caller that would rather not hold the
 * processor for a whole frame - see GFXIOC_VSYNCTRY.  Spins at most
 * `us`, returns 1 if the top of blanking arrived and 0 if it did not,
 * so the caller can come back round having been preemptible in
 * between.  A frame is 16.7ms and this kernel cannot preempt inside a
 * syscall, so the difference is whether the MOD player gets to run.
 */
int display_wait_vblank_try(unsigned int us)
{
    uint64_t dl;

    if (us > 20000u)
        us = 20000u;
    dl = time_us_64() + us;
    while (v_scanline != 0) {
        if (time_us_64() >= dl)
            return 0;
        tight_loop_contents();
    }
    return 1;
}


/* --- the seam: what the portable half asks of the hardware --------------- */
/*
 * These five are display_gfx_mode() and the palette functions taken
 * apart at the lines where they touched the scanout, and nothing else:
 * the sequence the core runs - stop or wait, tables, framebuffer, then
 * the gfx_exp store, the barrier, then restart - is the sequence the
 * one-file version ran, in the same order, with the same barrier in
 * the same place.  Palette, table and framebuffer are all ready BEFORE
 * core1 is told to use them, and gfx_exp is still the handover flag;
 * the core writes it, because the core owns it, and this side only
 * makes the write visible.
 */
static struct vtiming *tim_next;

int disp_hw_mode_prepare(int raster)
{
    int rebuild;

    tim_next = (raster == DISP_RASTER_XGA) ? &tim_xga : &tim_vga;
    rebuild = (tim_next != tim);
    if (rebuild)
        disp_scanout_stop();
    else
        gfx_wait_vblank();
    return rebuild;
}

void disp_hw_mode_tables(enum gexp ex)
{
    gfx_lut_rebuild(ex);
}

void disp_hw_mode_handover(enum gexp ex, int raster)
{
    /* The core has just stored gfx_exp - the handover flag core1
     * reads every scanline.  The barrier makes that store, and the
     * palette, table and framebuffer stores before it, visible before
     * the timing changes.  prepare() already chose tim_next. */
    (void)ex;
    (void)raster;
    __dmb();
    tim = tim_next;
}

void disp_hw_mode_finish(int rebuild)
{
    if (rebuild)
        disp_scanout_start();
}

void disp_hw_palette_changed(enum gexp ex)
{
    gfx_lut_rebuild(ex);
}

void display_init(void)
{
    dmach_ping = dma_claim_unused_channel(true);
    dmach_pong = dma_claim_unused_channel(true);

    disp_scanout_start();

    kputs("HDMI display: 640x480 console, graphics modes 0-5 and 7\n");
}

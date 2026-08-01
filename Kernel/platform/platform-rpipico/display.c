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

#include <hardware/dma.h>
#include <hardware/resets.h>
#include <hardware/structs/hstx_ctrl.h>
#include <hardware/structs/hstx_fifo.h>
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
uint8_t disp_fb[DISP_FB_POOL];
uint8_t disp_tile_fg[DISP_ROWS * DISP_COLS];
uint8_t disp_tile_bg[DISP_ROWS * DISP_COLS];

/* RGB332 expanded scanlines.  Word-aligned: the expanders write them
 * through uint32_t stores. */
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

/* Mask applied to a physical colour number in this expander: MODE 7
 * reaches all 16, every BBC mode only the authentic 8. */
static uint8_t gfx_physmask(enum gexp ex)
{
    return (ex == EXP_4BPP_X2) ? 15 : 7;
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

/* --- DMA IRQ (runs on core1): post the next scanline --------------------- */
static void __not_in_flash_func(disp_dma_irq)(void)
{
    uint ch_num = dma_pong ? dmach_pong : dmach_ping;
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    dma_hw->ints1 = 1u << ch_num;
    dma_pong = !dma_pong;

    int blanking = tim->vfp + tim->vsync + tim->vbp;

    if (v_scanline >= tim->vfp && v_scanline < (tim->vfp + tim->vsync)) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_on;
        ch->transfer_count = count_of(vblank_line_vsync_on);
    } else if (v_scanline < blanking) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_off;
        ch->transfer_count = count_of(vblank_line_vsync_off);
    } else if (!vactive_cmdlist_posted) {
        ch->read_addr = (uintptr_t)vactive_line;
        ch->transfer_count = count_of(vactive_line);
        vactive_cmdlist_posted = true;
    } else {
        ch->read_addr = (uintptr_t)disp_lines[v_scanline & 1];
        ch->transfer_count = tim->hact / 4; /* 4 RGB332 px per word */
        vactive_cmdlist_posted = false;
    }

    if (!vactive_cmdlist_posted) {
        v_scanline = (v_scanline + 1) % tim->vtotal;
    }
}

/* --- core1 fill loop: expand one scanline into RGB332 -------------------- */
static void __not_in_flash_func(disp_fill_loop)(void)
{
    int last_line = 2;
    for (;;) {
        if (v_scanline != last_line) {
            last_line = v_scanline;
            int active = last_line - (tim->vtotal - tim->vact);
            if (active < 0 || active >= tim->vact)
                continue;
            uint8_t *p = disp_lines[last_line & 1];

            switch (gfx_exp) {
            case EXP_CONSOLE: {
                const uint8_t *s = &disp_fb[active * DISP_STRIDE];
                const uint8_t *fg = &disp_tile_fg[(active / DISP_CELL_H) * DISP_COLS];
                const uint8_t *bg = &disp_tile_bg[(active / DISP_CELL_H) * DISP_COLS];
                for (int t = 0; t < DISP_COLS; t++) {
                    uint8_t b = s[t];
                    uint8_t f = fg[t];
                    uint8_t k = bg[t];
                    p[0] = (b & 0x80) ? f : k;
                    p[1] = (b & 0x40) ? f : k;
                    p[2] = (b & 0x20) ? f : k;
                    p[3] = (b & 0x10) ? f : k;
                    p[4] = (b & 0x08) ? f : k;
                    p[5] = (b & 0x04) ? f : k;
                    p[6] = (b & 0x02) ? f : k;
                    p[7] = (b & 0x01) ? f : k;
                    p += 8;
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
            gfx_pal[i] = bbc_rgb332[defpal[pal][i] & gfx_physmask(exp)];
        /* Palette, table and framebuffer all ready BEFORE core1 is
         * told to use them - gfx_exp is the handover, and nothing the
         * expanders read may still be stale when it changes. */
        gfx_lut_rebuild(exp);
        memset(disp_fb, 0, (int)stride * rows);
        console_gfx(1);
        gfx_exp = exp;
        __dmb();
        tim = newtim;
    }

    if (rebuild)
        disp_scanout_start();
    return (int)stride * rows;
}

/* Set logical colour -> physical colour.  Modes 0-5 take the authentic
 * BBC 0-7 (8-15 flash on real hardware; here they map steady); MODE 7
 * takes all 16. */
void display_gfx_pal(uint8_t logical, uint8_t physical)
{
    enum gexp ex = gfx_exp;

    gfx_pal[logical & 15] = bbc_rgb332[physical & gfx_physmask(ex)];
    gfx_lut_rebuild(ex);
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

static int gfx_bpp(enum gexp ex)
{
    return (ex == EXP_CONSOLE || ex == EXP_1BPP_5TO8) ? 1 : 4;
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

void display_gfx_colour(uint32_t rgb888)
{
    enum gexp ex = gfx_exp;
    uint8_t want, best = 0, i;
    int bestd = 0x7FFF;
    uint8_t r, g, b;

    if (gfx_bpp(ex) == 1) {
        /* Two colours: anything that is not black is ink.  This is what
         * MMBasic's DrawPixel2 does with its `if (c)`. */
        gfx_curcol = rgb888 ? 1 : 0;
        return;
    }

    /* 4bpp here is PALETTISED - gfx_pal[] maps 16 logical colours to
     * RGB332 - so there is no fixed encoding to apply.  Reduce the
     * request to RGB332 and take the nearest palette entry, which
     * works whatever VDU19 has done to the palette.  16 comparisons,
     * once per colour change. */
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
    gfx_curcol = best;
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
        v = disp_fb[y * stride + (x >> 1)];
        return (int)rgb332_to_888(gfx_pal[(x & 1) ? (v & 15) : (v >> 4)]);
    }
    /* 1bpp: the bit chooses ink or paper, and the actual colour lives
     * in the cell's tile attributes - so read those rather than
     * pretending the console is black and white. */
    v = disp_fb[y * stride + (x >> 3)];
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
        p = &disp_fb[y * stride + (x >> 1)];
        if (x & 1)
            *p = (*p & 0xF0) | (c & 15);        /* odd  -> low nibble */
        else
            *p = (*p & 0x0F) | ((c & 15) << 4); /* even -> high nibble */
    } else {
        p = &disp_fb[y * stride + (x >> 3)];
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
    int x, y, t;

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
            uint8_t *row = &disp_fb[y * stride];
            x = x1;
            if (x & 1) {        /* odd left edge: low nibble only */
                row[x >> 1] = (row[x >> 1] & 0xF0) | (c & 15);
                x++;
            }
            /* whole bytes are two pixels at a time */
            while (x + 1 <= x2) {
                row[x >> 1] = both;
                x += 2;
            }
            if (x <= x2)        /* even right edge: high nibble only */
                row[x >> 1] = (row[x >> 1] & 0x0F) | ((c & 15) << 4);
        }
    } else {
        for (y = y1; y <= y2; y++) {
            uint8_t *row = &disp_fb[y * stride];
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

void display_init(void)
{
    dmach_ping = dma_claim_unused_channel(true);
    dmach_pong = dma_claim_unused_channel(true);

    disp_scanout_start();

    kputs("HDMI display: 640x480 console, graphics modes 0-5 and 7\n");
}

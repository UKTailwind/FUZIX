/*
 * Pico Computer 3 HDMI display for Fuzix.
 *
 * Two personalities share the scanout machinery (HSTX TMDS encode, sync
 * command lists, ping-pong scanline DMA, core1 line expansion):
 *
 *  - Console: 640x480, 1bpp + RGB332 fg/bg per 8x12 cell (80x40), at
 *    clk_hstx = clk_sys/2.  324 MHz -> 32.4 MHz pixel -> 77.1 Hz.
 *  - BBC graphics modes 0-5: 1024x768 VESA timing at clk_hstx = clk_sys
 *    (full-rate DDR: 324 MHz -> 64.8 MHz pixel -> 59.9 Hz).  The
 *    framebuffer stays at BBC resolution (20K/40K, sharing the console
 *    framebuffer allocation) and core1 expands each output scanline
 *    with a palette lookup table:
 *      modes 1/4: 320x256, 4bpp -> x3 h, x3 v, 960 wide + 32px borders
 *      modes 2/5: 160x256, 4bpp -> x6 h, x3 v, ditto
 *      modes 0/3: 640x256, 1bpp -> x1 h, x2 v, centred 640x512
 *    4bpp layout: high nibble = left pixel.  1bpp: MSB = left.
 *
 * Core1 is owned by the display; nothing else may run there.  Mode
 * switching stops and relaunches core1 with the new timing.
 */

#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include "picosdk.h"
#include "config.h"
#include "display.h"

#include <hardware/dma.h>
#include <hardware/structs/hstx_ctrl.h>
#include <hardware/structs/hstx_fifo.h>
#include <pico/multicore.h>

/* --- video timing -------------------------------------------------------- */
struct vtiming {
    uint16_t hfp, hsync, hbp, hact;
    uint16_t vfp, vsync, vbp, vact, vtotal;
    uint8_t  hstx_div;          /* clk_sys / this -> clk_hstx */
};

static const struct vtiming tim_vga = {
    16, 96, 48, 640,  10, 2, 33, 480, 525,  2
};
static const struct vtiming tim_xga = {   /* VESA 1024x768 (1344x806) */
    24, 136, 160, 1024,  3, 6, 29, 768, 806,  1
};
static const struct vtiming *tim = &tim_vga;

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

static uint8_t disp_lines[2][1024]; /* RGB332 expanded scanlines */

static uint32_t vblank_line_vsync_off[7];
static uint32_t vblank_line_vsync_on[7];
static uint32_t vactive_line[9];

static volatile int32_t v_scanline = 2;
static volatile bool dma_pong = false;
static volatile bool vactive_cmdlist_posted = false;
static int dmach_ping = -1, dmach_pong = -1;

#define CORE1_STACK_WORDS 1024 /* 4 KB */
#define STACK_SENTINEL    0xf00dbeefu
static uint32_t disp_core1_stack[CORE1_STACK_WORDS] __attribute__((aligned(8)));

/* --- BBC graphics state -------------------------------------------------- */
enum gexp {
    EXP_CONSOLE = 0,
    EXP_4BPP_X3,        /* modes 1/4: 320 wide, 160 bytes/line */
    EXP_4BPP_X6,        /* modes 2/5: 160 wide, 80 bytes/line  */
    EXP_1BPP_X1,        /* modes 0/3: 640 wide, 80 bytes/line, x2 lines */
};
static volatile enum gexp gfx_exp = EXP_CONSOLE;
static uint8_t gfx_pal[16];
static uint16_t gfx_stride;                 /* source bytes per BBC line */

/* One shared expansion LUT, 16-byte stride for shift-indexing:
 *  4BPP_X3: byte -> 6 output bytes;  4BPP_X6: 12;  1BPP_X1: 8. */
static uint8_t gfx_lut[256][16];

/* BBC physical colours 0-7 in RGB332 (8-15 = the flashing set, mapped
 * to their steady counterparts) */
static const uint8_t bbc_rgb332[8] = {
    0x00, 0xE0, 0x1C, 0xFC, 0x03, 0xE3, 0x1F, 0xFF
};

static void gfx_lut_rebuild(void)
{
    int b, i;
    switch (gfx_exp) {
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
    case EXP_1BPP_X1:
        for (b = 0; b < 256; b++) {
            uint8_t *e = gfx_lut[b];
            for (i = 0; i < 8; i++)
                e[i] = (b & (0x80 >> i)) ? gfx_pal[1] : gfx_pal[0];
        }
        break;
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
            case EXP_1BPP_X1: {
                /* 640x512 centred in 768 lines */
                if (active < 128 || active >= 640) {
                    memset(p, 0, 1024);
                    break;
                }
                const uint8_t *s = &disp_fb[((active - 128) / 2) * 80];
                memset(p, 0, 192);
                p += 192;
                for (int t = 0; t < 80; t++) {
                    const uint8_t *e = gfx_lut[s[t]];
                    *(uint32_t *)p = *(const uint32_t *)e;
                    *(uint32_t *)(p + 4) = *(const uint32_t *)(e + 4);
                    p += 8;
                }
                memset(p, 0, 192);
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
    uint32_t hstx_target;
    if (tim->hstx_div == 1) {
        hstx_target = hstx_in;
    } else if (hstx_in > 350 * 1000000u) {
        /* 378 MHz: /2 would be far too fast; fractional to ~125.5 MHz */
        hstx_target = (uint32_t)(((uint64_t)hstx_in * 332) / 1000);
    } else {
        hstx_target = hstx_in / 2;
    }
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

    static const int lane_to_bit[3] = { PC3_HDMI_D0, PC3_HDMI_D1, PC3_HDMI_D2 };
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
}

/* --- Public -------------------------------------------------------------- */
bool display_in_blanking(void)
{
    return v_scanline < (tim->vfp + tim->vsync + tim->vbp);
}

bool display_stack_ok(void)
{
    return disp_core1_stack[0] == STACK_SENTINEL;
}

/* Enter a BBC graphics mode (0-5), or 0xFF back to the text console.
 * Returns the framebuffer size, or -1 for a bad mode. */
int display_gfx_mode(int mode)
{
    extern void console_gfx(int active);    /* console.c */
    enum gexp exp;
    int size;

    switch (mode) {
    case 0: case 3:
        exp = EXP_1BPP_X1;
        gfx_stride = 80;
        size = 80 * 256;
        break;
    case 1: case 4:
        exp = EXP_4BPP_X3;
        gfx_stride = 160;
        size = 160 * 256;
        break;
    case 2: case 5:
        exp = EXP_4BPP_X6;
        gfx_stride = 80;
        size = 80 * 256;
        break;
    case 0xFF:
        exp = EXP_CONSOLE;
        size = 0;
        break;
    default:
        return -1;
    }

    disp_scanout_stop();
    gfx_exp = exp;

    if (exp == EXP_CONSOLE) {
        tim = &tim_vga;
        console_gfx(0);         /* clears and repaints the console */
    } else {
        static const uint8_t defpal[3][16] = {
            /* modes 0/3: black, white */
            { 0, 7, 0, 7, 0, 7, 0, 7, 0, 7, 0, 7, 0, 7, 0, 7 },
            /* modes 1/4: black, red, yellow, white */
            { 0, 1, 3, 7, 0, 1, 3, 7, 0, 1, 3, 7, 0, 1, 3, 7 },
            /* modes 2/5: the full set */
            { 0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7 },
        };
        const uint8_t *dp = defpal[exp == EXP_1BPP_X1 ? 0 :
                                   exp == EXP_4BPP_X3 ? 1 : 2];
        for (int i = 0; i < 16; i++)
            gfx_pal[i] = bbc_rgb332[dp[i] & 7];
        tim = &tim_xga;
        console_gfx(1);
        memset(disp_fb, 0, size);
        gfx_lut_rebuild();
    }

    disp_scanout_start();
    return size;
}

/* Set logical colour -> BBC physical colour (0-15; 8-15 map steady). */
void display_gfx_pal(uint8_t logical, uint8_t physical)
{
    gfx_pal[logical & 15] = bbc_rgb332[physical & 7];
    gfx_lut_rebuild();
}

/* Current graphics framebuffer size (0 = console mode). */
int display_gfx_size(void)
{
    return (gfx_exp == EXP_CONSOLE) ? 0 : gfx_stride * 256;
}

void display_init(void)
{
    dmach_ping = dma_claim_unused_channel(true);
    dmach_pong = dma_claim_unused_channel(true);

    disp_scanout_start();

    kputs("HDMI display: 640x480 console, BBC graphics modes 0-5\n");
}

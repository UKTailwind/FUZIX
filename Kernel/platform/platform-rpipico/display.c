/*
 * Pico Computer 3 HDMI display for Fuzix: 640x480, 1 bit per pixel, with
 * RGB332 foreground/background colours per 8x12 character cell (the
 * PicoMiteVGA MODE 1 idea, cell-aligned for the console).
 *
 * The scanout machinery - HSTX TMDS encode, sync command lists, ping-pong
 * scanline DMA, core1 line expansion - follows the Pico Computer 3
 * MicroPython driver (ports/rp2/hdmi_rp2.c), which in turn follows
 * MMBasic's HDMI.c. Core1 is owned by the display: it expands each active
 * line (1bpp + cell colours -> RGB332 bytes) into a double-buffered line
 * buffer that HSTX scans; nothing else may run there.
 *
 * Pixel clock = clk_hstx / 5, clk_hstx = clk_sys / 2 (except 378 MHz,
 * which needs the fractional divider):
 *   252 MHz -> 25.2 MHz pixel -> 640x480@60
 *   315 MHz -> 31.5 MHz pixel -> 640x480@75
 *   378 MHz -> 25.1 MHz pixel -> 640x480@60
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

/* --- 640x480 timing (both 60 and 75 Hz run this 800x525 frame) ---------- */
#define H_FRONT_PORCH   16
#define H_SYNC_WIDTH    96
#define H_BACK_PORCH    48
#define H_ACTIVE        640
#define V_FRONT_PORCH   10
#define V_SYNC_WIDTH    2
#define V_BACK_PORCH    33
#define V_ACTIVE        480
#define V_TOTAL         525
#define BLANKING_COUNT  (V_FRONT_PORCH + V_SYNC_WIDTH + V_BACK_PORCH)

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
uint8_t disp_fb[DISP_HEIGHT * DISP_STRIDE];
uint8_t disp_tile_fg[DISP_ROWS * DISP_COLS];
uint8_t disp_tile_bg[DISP_ROWS * DISP_COLS];

static uint8_t disp_lines[2][H_ACTIVE]; /* RGB332 expanded scanlines */

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

/* --- DMA IRQ (runs on core1): post the next scanline --------------------- */
static void __not_in_flash_func(disp_dma_irq)(void)
{
    uint ch_num = dma_pong ? dmach_pong : dmach_ping;
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    dma_hw->ints1 = 1u << ch_num;
    dma_pong = !dma_pong;

    if (v_scanline >= V_FRONT_PORCH && v_scanline < (V_FRONT_PORCH + V_SYNC_WIDTH)) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_on;
        ch->transfer_count = count_of(vblank_line_vsync_on);
    } else if (v_scanline < BLANKING_COUNT) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_off;
        ch->transfer_count = count_of(vblank_line_vsync_off);
    } else if (!vactive_cmdlist_posted) {
        ch->read_addr = (uintptr_t)vactive_line;
        ch->transfer_count = count_of(vactive_line);
        vactive_cmdlist_posted = true;
    } else {
        ch->read_addr = (uintptr_t)disp_lines[v_scanline & 1];
        ch->transfer_count = H_ACTIVE / 4; /* 4 RGB332 px per word */
        vactive_cmdlist_posted = false;
    }

    if (!vactive_cmdlist_posted) {
        v_scanline = (v_scanline + 1) % V_TOTAL;
    }
}

/* --- core1 fill loop: expand 1bpp + cell colours into RGB332 ------------- */
static void __not_in_flash_func(disp_fill_loop)(void)
{
    int last_line = 2;
    for (;;) {
        if (v_scanline != last_line) {
            last_line = v_scanline;
            int active = last_line - (V_TOTAL - V_ACTIVE);
            if (active >= 0 && active < V_ACTIVE) {
                uint8_t *p = disp_lines[last_line & 1];
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
            }
        }
    }
}

/* --- core1 entry: clk_hstx, HSTX, DMA, IRQ, then the fill loop ----------- */
static void __not_in_flash_func(disp_core1_entry)(void)
{
    uint32_t hstx_in = clock_get_hz(clk_sys);
    uint32_t hstx_target;
    if (hstx_in > 350 * 1000000u) {
        /* 378 MHz: /2 would be far too fast; fractional to ~125.5 MHz */
        hstx_target = (uint32_t)(((uint64_t)hstx_in * 332) / 1000);
    } else {
        hstx_target = hstx_in / 2;
    }
    clock_configure(clk_hstx, 0,
        CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
        hstx_in, hstx_target);

    vblank_line_vsync_off[0] = HSTX_CMD_RAW_REPEAT | H_FRONT_PORCH;
    vblank_line_vsync_off[1] = SYNC_V1_H1;
    vblank_line_vsync_off[2] = HSTX_CMD_RAW_REPEAT | H_SYNC_WIDTH;
    vblank_line_vsync_off[3] = SYNC_V1_H0;
    vblank_line_vsync_off[4] = HSTX_CMD_RAW_REPEAT | (H_BACK_PORCH + H_ACTIVE);
    vblank_line_vsync_off[5] = SYNC_V1_H1;
    vblank_line_vsync_off[6] = HSTX_CMD_NOP;

    vblank_line_vsync_on[0] = HSTX_CMD_RAW_REPEAT | H_FRONT_PORCH;
    vblank_line_vsync_on[1] = SYNC_V0_H1;
    vblank_line_vsync_on[2] = HSTX_CMD_RAW_REPEAT | H_SYNC_WIDTH;
    vblank_line_vsync_on[3] = SYNC_V0_H0;
    vblank_line_vsync_on[4] = HSTX_CMD_RAW_REPEAT | (H_BACK_PORCH + H_ACTIVE);
    vblank_line_vsync_on[5] = SYNC_V0_H1;
    vblank_line_vsync_on[6] = HSTX_CMD_NOP;

    vactive_line[0] = HSTX_CMD_RAW_REPEAT | H_FRONT_PORCH;
    vactive_line[1] = SYNC_V1_H1;
    vactive_line[2] = HSTX_CMD_NOP;
    vactive_line[3] = HSTX_CMD_RAW_REPEAT | H_SYNC_WIDTH;
    vactive_line[4] = SYNC_V1_H0;
    vactive_line[5] = HSTX_CMD_NOP;
    vactive_line[6] = HSTX_CMD_RAW_REPEAT | H_BACK_PORCH;
    vactive_line[7] = SYNC_V1_H1;
    vactive_line[8] = HSTX_CMD_TMDS | H_ACTIVE;

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

/* --- Public -------------------------------------------------------------- */
bool display_in_blanking(void)
{
    return v_scanline < BLANKING_COUNT;
}

bool display_stack_ok(void)
{
    return disp_core1_stack[0] == STACK_SENTINEL;
}

void display_init(void)
{
    dmach_ping = dma_claim_unused_channel(true);
    dmach_pong = dma_claim_unused_channel(true);

    v_scanline = 2;
    dma_pong = false;
    vactive_cmdlist_posted = false;
    disp_core1_stack[0] = STACK_SENTINEL;
    multicore_launch_core1_with_stack(disp_core1_entry,
        disp_core1_stack, sizeof(disp_core1_stack));

    kputs("HDMI display: 640x480 1bpp, 80x40 colour cells\n");
}

/*
 * QMI timing + PSRAM bring-up for the Pico Computer 3 (RP2350B).
 *
 * psram_detect/psram_init are vendored from the MicroPython rp2 port
 * (ports/rp2/rp2_psram.c, MIT licence):
 *
 * Copyright (c) 2025 Phil Howard
 *                    Mike Bell
 *                    Kirk D. Benell
 *
 * qmi_flash_timing follows the MicroPython rp2_flash.c RP2350 path.
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/structs/pads_qspi.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "psram.h"

/*
 * Re-cap the flash divisor for a given clk_sys.
 *
 * This used to say "call with nothing executing from flash - the kernel
 * is PICO_COPY_TO_RAM, so only the XIP window itself is at risk". That
 * is no longer true: the kernel now RUNS from flash, so the divisor has
 * to be right across the clock change as well as after it, which is
 * what pc3_clock_init below is for.
 */
static void __no_inline_not_in_flash_func(qmi_flash_timing_for)
        (uint32_t clock_hz, uint32_t max_flash_hz)
{
    uint32_t divisor = (clock_hz + max_flash_hz - 1) / max_flash_hz;

    /* Make sure flash is deselected - QMI doesn't have a busy flag */
    while ((ioqspi_hw->io[1].status & IO_QSPI_GPIO_QSPI_SS_STATUS_OUTTOPAD_BITS) !=
           IO_QSPI_GPIO_QSPI_SS_STATUS_OUTTOPAD_BITS) {
    }

    /* RX delay equal to the divisor samples on the next falling edge */
    qmi_hw->m[0].timing = (1 << QMI_M0_TIMING_COOLDOWN_LSB) |
        divisor << QMI_M0_TIMING_RXDELAY_LSB |
        divisor << QMI_M0_TIMING_CLKDIV_LSB;

    /* Force a read through XIP to ensure the timing is applied */
    volatile uint32_t *ptr = (volatile uint32_t *)0x14000000;
    (void)*ptr;
}

void __no_inline_not_in_flash_func(qmi_flash_timing)(uint32_t max_flash_hz)
{
    qmi_flash_timing_for(clock_get_hz(clk_sys), max_flash_hz);
}

/*
 * Raise clk_sys, keeping XIP alive across the change.
 *
 * The kernel executes from flash now, so instructions are being fetched
 * through the QMI while the clock underneath it moves.  The divisor in
 * force during the switch has to be safe for BOTH speeds, or the first
 * fetch after the PLL relocks is at a flash clock far over spec and the
 * machine simply stops - no panic, no console, which is exactly what
 * happened when this was left as "set the clock, then fix the divisor".
 *
 * This is MMBasic's sequence (PicoMite.c, the set_sys_clock_khz
 * wrapper), taken rather than re-derived: conservative timing for
 * max(old, new) BEFORE the switch, then the relaxed timing for the new
 * clock after it - and the QSPI pad drive set on both sides, because
 * set_sys_clock can rewrite the pads.
 *
 * Must not be inlined into a caller that lives in flash.
 */
void __no_inline_not_in_flash_func(pc3_clock_init)(uint32_t target_khz,
                                                   uint32_t max_flash_hz)
{
    uint32_t old_hz = clock_get_hz(clk_sys);
    uint32_t new_hz = target_khz * 1000u;
    uint32_t worst = (old_hz > new_hz) ? old_hz : new_hz;

    /*
     * CORE VOLTAGE FIRST, and it must settle before the clock moves.
     *
     * The chip leaves reset at 1.10 V and this kernel runs at 375 MHz.
     * The PC3 does not depend on the internal regulator - DVDD there is
     * fed externally at 1.3 V - but a clone on a stock module such as a
     * Pimoroni PGA2350 has nothing else, and at 375 MHz on 1.10 V it
     * does not run at all.  That is the reported failure, and it is why
     * this is here rather than left to the board.
     *
     * MMBasic's ladder (PicoMite.c), except that it uses 1.60 V above
     * 320 MHz and this uses 1.40 - enough over the 1.3 the PC3 supplies
     * externally at this speed, without asking a clone for MMBasic's
     * full headroom until someone reports needing it.
     *
     * vreg_disable_voltage_limit() first: anything above 1.30 V sits
     * behind POWMAN's limit and vreg_set_voltage() would clamp.
     *
     * Safe to call the SDK's flash-resident vreg code from this
     * RAM-resident function: XIP is still live and stays live until
     * set_sys_clock_khz below, which is the whole reason for the
     * ordering in this function.
     */
    vreg_disable_voltage_limit();
    if (target_khz <= 200000)
        vreg_set_voltage(VREG_VOLTAGE_1_15);
    else if (target_khz <= 320000)
        vreg_set_voltage(VREG_VOLTAGE_1_30);
    else
        vreg_set_voltage(VREG_VOLTAGE_1_40);
    busy_wait_us(10000);        /* MMBasic waits 10 ms; so do we */

    /* Drive strength and slew for a fast QSPI clock - MMBasic's values */
    pads_qspi_hw->io[0] = 0x67;
    pads_qspi_hw->io[1] = 0x67;
    pads_qspi_hw->io[2] = 0x67;
    pads_qspi_hw->io[3] = 0x6B;
    pads_qspi_hw->io[4] = 0x6B;
    pads_qspi_hw->io[5] = 0x6B;

    /* Safe for whichever of the two clocks is faster: the write happens
     * at the old speed and has to survive until the new one is live. */
    qmi_flash_timing_for(worst, max_flash_hz);
    busy_wait_us(2);

    set_sys_clock_khz(target_khz, true);

    /*
     * clk_peri FOLLOWS clk_sys, and it has to be said here because the
     * SDK does the opposite: set_sys_clock_pll parks clk_peri on
     * PLL_USB at 48 MHz unless PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK
     * is defined, and that option defaults to off.
     *
     * Everything clocked from clk_peri is then capped at 48 MHz however
     * fast the chip runs - SPI at 24 MHz, because the divisor bottoms
     * out at clk_peri / 2.  That is what a display benchmark showed:
     * asking for 31.25, 40, 50 and 62.5 MHz all produced exactly 24,
     * and asking for 10 and 20 produced 8 and 12, which are 48/6 and
     * 48/4.  This file has claimed "clk_peri follows clk_sys, as in the
     * PC3's MicroPython/MMBasic firmwares" since the clock was first
     * raised, and it simply was not true.
     *
     * MMBasic really does run it from clk_sys - it reports the CPU
     * speed by MEASURING clk_peri (frequency_count_khz of
     * CLOCKS_FC0_SRC_VALUE_CLK_PERI in External.c), which only makes
     * sense if the two are the same.
     *
     * ORDER MATTERS: this must happen before anything derives a divisor
     * from clk_peri.  The UART comes up in devtty_early_init() after
     * this returns, and the SD card's SPI1 later still, so both pick up
     * the new rate.  Moving it later would leave the console computing
     * its divisor from 48 MHz and talking gibberish.
     */
    clock_configure_undivided(clk_peri, 0,
                              CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                              new_hz);

    /* set_sys_clock can rewrite the QSPI pads, so put them back, and
     * now relax the divisor to what the new clock actually needs. */
    pads_qspi_hw->io[0] = 0x67;
    pads_qspi_hw->io[1] = 0x67;
    pads_qspi_hw->io[2] = 0x67;
    pads_qspi_hw->io[3] = 0x6B;
    pads_qspi_hw->io[4] = 0x6B;
    pads_qspi_hw->io[5] = 0x6B;
    qmi_flash_timing_for(new_hz, max_flash_hz);
}

/*
 * The PSRAM's own timing limits, by manufacturer - PicoMite's table
 * (io/psram.c, 13b081e), from the parts' datasheets: the fastest clock,
 * tCEM (the longest a select may last, the refresh limit) and tCPM (the
 * shortest deselect), the last in femtoseconds or, for a part that
 * gives it in cycles, as a count.
 *
 * This used to be MicroPython's arithmetic for a 133 MHz part, which
 * at this kernel's 378 MHz gave divider 3 - a 126 MHz clock for the
 * APS6404's 109 - and took half a clock off the 18 ns deselect.  It
 * ran, which is not the same as being inside the part's limits.
 */
#define MFID_APMEMORY   0x0D
#define MFID_ISSI       0x9D

struct psram_chip {
    uint8_t mfid;
    uint32_t max_clk;
    uint64_t max_select_fs;
    uint32_t min_deselect_fs;
    int8_t min_deselect_cs;     /* >= 0: in cycles, overrides _fs */
};

static const struct psram_chip psram_chips[] = {
    { MFID_APMEMORY, 109000000, 8000000000ull, 18000000, -1 },
    { MFID_ISSI,     104000000, 4000000000ull, 0,         1 },
    { 0,             109000000, 8000000000ull, 50000000, -1 },  /* unknown */
};

/*
 * M1_TIMING for a part at clk_sys = hz, as PicoMite's psram_timing:
 * the divider brings the clock under the part's maximum, max select is
 * tCEM in units of 64 system clocks, min deselect is tCPM rounded up
 * to whole system clocks, and above 150 MHz the read is sampled the
 * divider's worth of clocks later.  At 378 MHz on an APS6404 that is
 * divider 4 (94.5 MHz), max select 47, deselect 7, rxdelay 5:
 * 0x61DE7504.  Worked out here, in flash, before direct mode - a
 * 64-bit divide is a library call.
 */
static uint32_t psram_timing(const struct psram_chip *chip, uint32_t hz)
{
    uint32_t fs = (uint32_t)(1000000000000000ull / hz);
    uint32_t clkdiv = (hz + chip->max_clk - 1) / chip->max_clk;
    uint32_t max_select = (uint32_t)((chip->max_select_fs >> 6) / fs);
    uint32_t min_deselect = (chip->min_deselect_fs + fs - 1) / fs;
    uint32_t rxdelay = 1 + (hz > 150000000 ? clkdiv : 1);

    if (chip->min_deselect_cs >= 0)
        min_deselect = (uint32_t)chip->min_deselect_cs;
    return 1 << QMI_M1_TIMING_COOLDOWN_LSB |
        QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB |
        0 << QMI_M1_TIMING_SELECT_SETUP_LSB |
        3 << QMI_M1_TIMING_SELECT_HOLD_LSB |
        max_select << QMI_M1_TIMING_MAX_SELECT_LSB |
        min_deselect << QMI_M1_TIMING_MIN_DESELECT_LSB |
        rxdelay << QMI_M1_TIMING_RXDELAY_LSB |
        clkdiv << QMI_M1_TIMING_CLKDIV_LSB;
}

/*
 * Every wait on the QMI in direct mode is bounded.  The flags are
 * normally clear within a microsecond; the bound is what keeps a probe
 * of a pin with nothing on it (board detection asks GP47 and then GP8)
 * from hanging the boot, as PicoMite found its unbounded waits could.
 * A loop count, not a timer: XIP is off in direct mode, so nothing in
 * flash may be called.  2^20 turns is a few milliseconds at 378 MHz.
 */
#define QMI_WAIT_TURNS  (1u << 20)

static int __no_inline_not_in_flash_func(qmi_wait_clear)(uint32_t bits)
{
    uint32_t n = QMI_WAIT_TURNS;

    while (qmi_hw->direct_csr & bits)
        if (--n == 0)
            return -1;
    return 0;
}

static int __no_inline_not_in_flash_func(qmi_wait_set)(uint32_t bits)
{
    uint32_t n = QMI_WAIT_TURNS;

    while (!(qmi_hw->direct_csr & bits))
        if (--n == 0)
            return -1;
    return 0;
}

/*
 * Read the ID of whatever is on chip select 1: manufacturer, known
 * good die (0x5D when a part answered) and the extended ID that holds
 * the density.  Interrupts off and the pin already on XIP_CS1.
 * Returns -1 if the QMI never finished a transfer.
 */
static int __no_inline_not_in_flash_func(psram_read_id)(uint8_t *mfid,
                                                        uint8_t *kgd,
                                                        uint8_t *eid)
{
    int bad = 0;

    *mfid = *kgd = *eid = 0;
    qmi_hw->direct_csr = 30 << QMI_DIRECT_CSR_CLKDIV_LSB | QMI_DIRECT_CSR_EN_BITS;

    /* Poll for the cooldown on the last XIP transfer to expire before it
     * is safe to perform the first direct-mode operation */
    bad |= qmi_wait_clear(QMI_DIRECT_CSR_BUSY_BITS);

    /* Exit out of QPI in case we've inited already */
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;

    /* Transmit as quad. */
    qmi_hw->direct_tx = QMI_DIRECT_TX_OE_BITS |
        QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB | 0xf5;

    bad |= qmi_wait_clear(QMI_DIRECT_CSR_BUSY_BITS);

    (void)qmi_hw->direct_rx;

    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS);

    /* Read the id: 0x9F, three address bytes, then MFID, KGD, EID */
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;

    for (size_t i = 0; i < 7 && !bad; i++) {
        uint8_t rx;

        qmi_hw->direct_tx = (i == 0) ? 0x9f : 0xff;
        bad |= qmi_wait_set(QMI_DIRECT_CSR_TXEMPTY_BITS);
        bad |= qmi_wait_clear(QMI_DIRECT_CSR_BUSY_BITS);
        rx = qmi_hw->direct_rx;
        if (i == 4)
            *mfid = rx;
        else if (i == 5)
            *kgd = rx;
        else if (i == 6)
            *eid = rx;
    }

    /* Disable direct csr. */
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS | QMI_DIRECT_CSR_EN_BITS);
    return bad ? -1 : 0;
}

/*
 * Is there a PSRAM with its chip select on GPgp?  Only the read-ID,
 * and the pin is handed back: board detection (board.c) asks GP47,
 * where the Pico Computer 2 and 3 have theirs, and then GP8, where the
 * Olimex RP2350pc has - PicoMite's psram_probe.  Called before the
 * console UART exists, which matters: on the Olimex GP8 is this chip
 * select and on the PC3 it is the console's TX.
 */
int psram_probe(unsigned int gp)
{
    uint8_t mfid, kgd, eid;
    uint32_t ints;
    int r;

    gpio_set_function(gp, GPIO_FUNC_XIP_CS1);
    ints = save_and_disable_interrupts();
    r = psram_read_id(&mfid, &kgd, &eid);
    restore_interrupts(ints);
    gpio_set_function(gp, GPIO_FUNC_NULL);
    return r == 0 && kgd == 0x5D;
}

/* QPI on, the timing, the read and write formats, writes enabled. */
static void __no_inline_not_in_flash_func(psram_qmi_setup)(uint32_t timing)
{
    /* Enable direct mode, PSRAM CS, clkdiv of 10. */
    qmi_hw->direct_csr = 10 << QMI_DIRECT_CSR_CLKDIV_LSB |
        QMI_DIRECT_CSR_EN_BITS |
        QMI_DIRECT_CSR_AUTO_CS1N_BITS;
    qmi_wait_clear(QMI_DIRECT_CSR_BUSY_BITS);

    /* Enable QPI mode on the PSRAM */
    const uint CMD_QPI_EN = 0x35;
    qmi_hw->direct_tx = QMI_DIRECT_TX_NOPUSH_BITS | CMD_QPI_EN;

    qmi_wait_clear(QMI_DIRECT_CSR_BUSY_BITS);

    qmi_hw->m[1].timing = timing;

    /* Set PSRAM commands and formats */
    qmi_hw->m[1].rfmt =
        QMI_M0_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_PREFIX_WIDTH_LSB |
        QMI_M0_RFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_RFMT_ADDR_WIDTH_LSB |
        QMI_M0_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_SUFFIX_WIDTH_LSB |
        QMI_M0_RFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_RFMT_DUMMY_WIDTH_LSB |
        QMI_M0_RFMT_DATA_WIDTH_VALUE_Q << QMI_M0_RFMT_DATA_WIDTH_LSB |
        QMI_M0_RFMT_PREFIX_LEN_VALUE_8 << QMI_M0_RFMT_PREFIX_LEN_LSB |
        6 << QMI_M0_RFMT_DUMMY_LEN_LSB;

    qmi_hw->m[1].rcmd = 0xEB;

    qmi_hw->m[1].wfmt =
        QMI_M0_WFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_PREFIX_WIDTH_LSB |
        QMI_M0_WFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_WFMT_ADDR_WIDTH_LSB |
        QMI_M0_WFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_SUFFIX_WIDTH_LSB |
        QMI_M0_WFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_WFMT_DUMMY_WIDTH_LSB |
        QMI_M0_WFMT_DATA_WIDTH_VALUE_Q << QMI_M0_WFMT_DATA_WIDTH_LSB |
        QMI_M0_WFMT_PREFIX_LEN_VALUE_8 << QMI_M0_WFMT_PREFIX_LEN_LSB;

    qmi_hw->m[1].wcmd = 0x38;

    /* Disable direct mode */
    qmi_hw->direct_csr = 0;

    /* Enable writes to PSRAM */
    hw_set_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_WRITABLE_M1_BITS);
}

size_t psram_init(unsigned int cs_pin)
{
    const struct psram_chip *chip = psram_chips;
    uint8_t mfid, kgd, eid;
    size_t psram_size;
    uint32_t ints, timing;
    int r;

    gpio_set_function(cs_pin, GPIO_FUNC_XIP_CS1);

    ints = save_and_disable_interrupts();
    r = psram_read_id(&mfid, &kgd, &eid);
    restore_interrupts(ints);
    if (r || kgd != 0x5D)
        return 0;

    psram_size = 1024 * 1024; /* 1 MiB */
    {
        uint8_t size_id = eid >> 5;
        if (eid == 0x26 || size_id == 2) {
            psram_size *= 8;
        } else if (size_id == 0) {
            psram_size *= 2;
        } else if (size_id == 1) {
            psram_size *= 4;
        }
    }

    while (chip->mfid && chip->mfid != mfid)
        chip++;
    timing = psram_timing(chip, clock_get_hz(clk_sys));

    ints = save_and_disable_interrupts();
    psram_qmi_setup(timing);
    restore_interrupts(ints);

    return psram_size;
}

/*
 *	How much of the bottom of the window the linker has already spent.
 *
 *	Variables marked __uninitialized_psram("group") are placed by
 *	sections_psram.incl between these two symbols, which start at
 *	PSRAM_BASE.  Both are always defined - the sections exist even
 *	when empty - so this returns 0 on a build that places nothing.
 *
 *	Rounded up to 4K to match the arena's granularity and to keep the
 *	disc's block arithmetic on a sensible boundary.
 */
extern char __psram_start__[];
extern char __psram_end__[];

uint32_t psram_static_len(void)
{
    uint32_t len = (uint32_t)__psram_end__ - (uint32_t)__psram_start__;

    return (len + 4095u) & ~4095u;
}

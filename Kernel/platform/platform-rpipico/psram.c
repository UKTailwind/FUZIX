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

static size_t __no_inline_not_in_flash_func(psram_detect)(void)
{
    int psram_size = 0;

    /* Try and read the PSRAM ID via direct_csr. */
    qmi_hw->direct_csr = 30 << QMI_DIRECT_CSR_CLKDIV_LSB | QMI_DIRECT_CSR_EN_BITS;

    /* Poll for the cooldown on the last XIP transfer to expire before it
     * is safe to perform the first direct-mode operation */
    while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0) {
    }

    /* Exit out of QMI in case we've inited already */
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;

    /* Transmit as quad. */
    qmi_hw->direct_tx = QMI_DIRECT_TX_OE_BITS |
        QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB | 0xf5;

    while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0) {
    }

    (void)qmi_hw->direct_rx;

    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS);

    /* Read the id */
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    uint8_t kgd = 0;
    uint8_t eid = 0;

    for (size_t i = 0; i < 7; i++) {
        if (i == 0) {
            qmi_hw->direct_tx = 0x9f;
        } else {
            qmi_hw->direct_tx = 0xff;
        }

        while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_TXEMPTY_BITS) == 0) {
        }

        while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0) {
        }

        if (i == 5) {
            kgd = qmi_hw->direct_rx;
        } else if (i == 6) {
            eid = qmi_hw->direct_rx;
        } else {
            (void)qmi_hw->direct_rx;
        }
    }

    /* Disable direct csr. */
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS | QMI_DIRECT_CSR_EN_BITS);

    if (kgd == 0x5D) {
        psram_size = 1024 * 1024; /* 1 MiB */
        uint8_t size_id = eid >> 5;
        if (eid == 0x26 || size_id == 2) {
            psram_size *= 8;
        } else if (size_id == 0) {
            psram_size *= 2;
        } else if (size_id == 1) {
            psram_size *= 4;
        }
    }

    return psram_size;
}

size_t __no_inline_not_in_flash_func(psram_init)(unsigned int cs_pin)
{
    gpio_set_function(cs_pin, GPIO_FUNC_XIP_CS1);

    uint32_t intr_stash = save_and_disable_interrupts();

    size_t psram_size = psram_detect();

    if (!psram_size) {
        restore_interrupts(intr_stash);
        return 0;
    }

    /* Read clock speed before entering direct mode (flash access is
     * unavailable while QMI direct mode is enabled) */
    const int max_psram_freq = 133000000;
    const int clock_hz = clock_get_hz(clk_sys);

    /* Enable direct mode, PSRAM CS, clkdiv of 10. */
    qmi_hw->direct_csr = 10 << QMI_DIRECT_CSR_CLKDIV_LSB |
        QMI_DIRECT_CSR_EN_BITS |
        QMI_DIRECT_CSR_AUTO_CS1N_BITS;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {
    }

    /* Enable QPI mode on the PSRAM */
    const uint CMD_QPI_EN = 0x35;
    qmi_hw->direct_tx = QMI_DIRECT_TX_NOPUSH_BITS | CMD_QPI_EN;

    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {
    }

    /* Set PSRAM timing for APS6404
     *
     * Using an rxdelay equal to the divisor isn't enough when running the
     * APS6404 close to 133MHz. So: don't allow running at divisor 1 above
     * 100MHz (because delay of 2 would be too late), and add an extra 1 to
     * the rxdelay if the divided clock is > 100MHz (i.e. sys clock >
     * 200MHz). */
    int divisor = (clock_hz + max_psram_freq - 1) / max_psram_freq;
    if (divisor == 1 && clock_hz > 100000000) {
        divisor = 2;
    }
    int rxdelay = divisor;
    if (clock_hz / divisor > 100000000) {
        rxdelay += 1;
    }

    /* - Max select must be <= 8us. The value is given in multiples of 64
     *   system clocks.
     * - Min deselect must be >= 18ns. The value is given in system clock
     *   cycles - ceil(divisor / 2). */
    const int clock_period_fs = 1000000000000000ll / clock_hz;
    const int max_select = (125 * 1000000) / clock_period_fs; /* 125 = 8000ns / 64 */
    const int min_deselect = (18 * 1000000 + (clock_period_fs - 1)) / clock_period_fs
        - (divisor + 1) / 2;

    qmi_hw->m[1].timing = 1 << QMI_M1_TIMING_COOLDOWN_LSB |
        QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB |
        max_select << QMI_M1_TIMING_MAX_SELECT_LSB |
        min_deselect << QMI_M1_TIMING_MIN_DESELECT_LSB |
        rxdelay << QMI_M1_TIMING_RXDELAY_LSB |
        divisor << QMI_M1_TIMING_CLKDIV_LSB;

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

    restore_interrupts(intr_stash);

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

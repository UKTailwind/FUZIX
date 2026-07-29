#include <stdint.h>
#include <stddef.h>
#include <kernel.h>
#include "lib/dhara/nand.h"
#include "picosdk.h"
#include <hardware/flash.h>
#include <hardware/structs/qmi.h>
#include <hardware/structs/xip_ctrl.h>
#include "globals.h"

/*
 *	Writing flash on an overclocked RP2350 damages two things that have
 *	nothing to do with flash, and both of them matter here.
 *
 *	1. boot2 re-runs inside flash_range_erase/flash_range_program and
 *	   leaves QMI M0 at CLKDIV=2. At 375MHz that is ~189MHz on the
 *	   flash SPI, well above spec, and nothing puts it back.
 *
 *	2. Those calls invalidate the XIP cache. psram.c sets
 *	   XIP_CTRL_WRITABLE_M1, so PSRAM is cached write-back - and the
 *	   swap device lives in PSRAM. Invalidating without cleaning first
 *	   discards every dirty swap line sitting in that cache.
 *
 *	Neither was handled, which is what "Warning, it's unstable" in
 *	config.h was really describing. This follows MMBasic's FileIO.c
 *	(save_psram_settings / restore_psram_settings and the safe_flash_*
 *	wrappers), which has been doing it on this silicon at this clock
 *	for a long time - taken as-is rather than re-derived.
 */

static uint32_t m0_timing, m0_rfmt, m1_timing, m1_rfmt;

static void __not_in_flash_func(flash_pre)(void)
{
    /* About to invalidate the XIP cache: clean it first so any dirty
       PSRAM writes are committed rather than thrown away. Bit 0 of the
       maintenance address selects clean, and the stride is one cache
       line. */
    uint8_t *maintenance_ptr = (uint8_t *)XIP_MAINTENANCE_BASE;
    int i;

    for (i = 1; i < 16 * 1024; i += 8)
        maintenance_ptr[i] = 0;

    m1_timing = qmi_hw->m[1].timing;
    m1_rfmt = qmi_hw->m[1].rfmt;
    m0_timing = qmi_hw->m[0].timing;
    m0_rfmt = qmi_hw->m[0].rfmt;
}

static void __not_in_flash_func(flash_post)(void)
{
    qmi_hw->m[1].timing = m1_timing;
    qmi_hw->m[1].rfmt = m1_rfmt;
    qmi_hw->m[0].timing = m0_timing;
    qmi_hw->m[0].rfmt = m0_rfmt;
}

int dhara_nand_erase(const struct dhara_nand *n, dhara_block_t b,
                     dhara_error_t *err)
{
    irqflags_t f = di();
    flash_pre();
    flash_range_erase(FLASH_OFFSET + (b*4096), 4096);
    flash_post();
    irqrestore(f);
	if (err)
		*err = DHARA_E_NONE;
	return 0;
}

int dhara_nand_prog(const struct dhara_nand *n, dhara_page_t p,
                    const uint8_t *data,
                    dhara_error_t *err)
{
    irqflags_t f = di();
    flash_pre();
    flash_range_program(FLASH_OFFSET + (p*512), data, 512);
    flash_post();
    irqrestore(f);
	if (err)
		*err = DHARA_E_NONE;
	return 0;
}

int dhara_nand_read(const struct dhara_nand *n, dhara_page_t p,
					size_t offset, size_t length,
                    uint8_t *data,
                    dhara_error_t *err)
{
    memcpy(data,
        (uint8_t*)XIP_NOCACHE_NOALLOC_BASE + FLASH_OFFSET + (p*512) + offset,
        length);
	if (err)
		*err = DHARA_E_NONE;
	return 0;
}

/* vim: sw=4 ts=4 et: */


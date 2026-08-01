/*
 * PSRAM disc for the Pico Computer 3: the 8 MiB QSPI PSRAM, XIP-mapped at
 * PSRAM_BASE by psram_init, exposed as a block device via the blkdev
 * framework (it registers after the NAND and SD drives, so it appears as
 * hdc). Contents do not survive power off: intended as fast swap -
 *   swapon /dev/hdc 16256
 * - or scratch space, not storage.
 */

#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <blkdev.h>
#include "config.h"
#include "psram.h"

static blkdev_t *psram_blk;

/*
 * The disc is a window into the XIP-mapped PSRAM, so an out-of-range
 * block is not an I/O error that gets reported - it is a memcpy to
 * wherever the arithmetic landed.  A swap block number that had gone
 * wild wrote 512 bytes at PSRAM_BASE + (lba << 9), off the end of the
 * window, and bus-faulted the machine with nothing to say for itself.
 * Refuse it instead, and name it: a short transfer is something the
 * callers already check.
 */
static uint_fast8_t psram_disc_transfer(void)
{
    uint8_t *pd;

    if (psram_blk && blk_op.lba >= psram_blk->drive_lba_count) {
        kprintf("psram: block %u beyond disc (%u), %s refused\n",
            (unsigned)blk_op.lba, (unsigned)psram_blk->drive_lba_count,
            blk_op.is_read ? "read" : "write");
        return 0;
    }
    pd = (uint8_t *)PSRAM_BASE + (blk_op.lba << 9);

    if (blk_op.is_read)
        memcpy(blk_op.addr, pd, 512);
    else
        memcpy(pd, blk_op.addr, 512);
    return 1;
}

/* psram_blk (declared above, where the transfer bounds-checks against
 * it) is kept so a "psram=" boot parameter can retune the split after
 * the probe has already registered the disc - the bootdev prompt comes
 * after device init but before rc's swapon, which is what matters. */

void psram_disc_resize(void)
{
    if (!psram_blk)
        return;
    if (arena_len > psram_size - PSRAM_RESERVE)
        arena_len = psram_size - PSRAM_RESERVE;
    psram_blk->drive_lba_count =
        (psram_size - PSRAM_RESERVE - arena_len) >> 9;
    kprintf("PSRAM: disc %dKiB, arena %dKiB, kernel %dKiB\n",
            (int)((psram_size - PSRAM_RESERVE - arena_len) >> 10),
            (int)(arena_len >> 10), (int)(PSRAM_RESERVE >> 10));
}

void psram_disc_init(void)
{
    blkdev_t *blk;

    if (psram_size <= PSRAM_RESERVE)
        return;
    blk = blkdev_alloc();
    if (!blk)
        return;

    /* Power-on PSRAM is random noise: clear the MBR and superblock
     * blocks so nothing mistakes it for a partitioned or filesystem
     * device (swapon checks both before accepting a swap device). */
    memset((void *)PSRAM_BASE, 0, 2048);

    blk->transfer = psram_disc_transfer;
    psram_blk = blk;
    /* three-way split: disc, then the userland arena, then the top
     * PSRAM_RESERVE bytes for the kernel (lineedit.c) */
    if (arena_len > psram_size - PSRAM_RESERVE)
        arena_len = psram_size - PSRAM_RESERVE;
    blk->drive_lba_count =
        (psram_size - PSRAM_RESERVE - arena_len) >> 9;
    kprintf("PSRAM disc %dKiB (arena %dKiB): ",
            (int)((psram_size - PSRAM_RESERVE - arena_len) >> 10),
            (int)(arena_len >> 10));
    blkdev_scan(blk, 0);
}

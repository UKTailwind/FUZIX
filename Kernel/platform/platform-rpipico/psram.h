#ifndef PC3_PSRAM_H
#define PC3_PSRAM_H

#include <stddef.h>
#include <stdint.h>

/* QMI-mapped PSRAM window (chip select 1) */
#define PSRAM_BASE 0x11000000u

/* Top of the PSRAM kept back from the disc/swap for kernel use:
 * console line-editor history (lineedit.c).  Swap loses nothing in
 * practice - 31 x 256K slots still fit and only 30 processes exist. */
#define PSRAM_RESERVE 65536u

/* Default size of the userland arena pool carved out below the kernel
 * reserve (PC3-PSRAM-ARENA.md); "psram=<n>[K|M]" at the bootdev prompt
 * overrides it, 0 disables.  1 MiB costs two swap slots (31 -> 29).
 * A stale rc whose swapon still claims the full disc gets EIO at the
 * boundary, not corruption - blkdev bounds-checks whole transfers. */
#define PSRAM_ARENA_DEFAULT (1024u * 1024u)

struct p_tab;
extern uint32_t arena_len;
uint32_t arena_pool_base(void);
uint32_t arena_alloc(struct p_tab *owner, uint32_t len);
int arena_free(struct p_tab *owner, uint32_t base);
void arena_release(struct p_tab *owner);
void arena_stat(uint32_t *total, uint32_t *freeb, uint32_t *largest);

/* Re-cap the flash (QMI CS0) divisor after clk_sys changes. */
void qmi_flash_timing(uint32_t max_flash_hz);

/* Bring up QSPI PSRAM on cs_pin (QMI CS1). Returns size in bytes, 0 if
 * absent. Timing derives from clk_sys, so raise the clock first. */
size_t psram_init(unsigned int cs_pin);

/* Detected size in bytes (set at boot in main.c), 0 if absent */
extern uint32_t psram_size;

/* Register the PSRAM as a block device (hdc) */
void psram_disc_init(void);

#endif

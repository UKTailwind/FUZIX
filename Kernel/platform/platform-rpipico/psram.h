#ifndef PC3_PSRAM_H
#define PC3_PSRAM_H

#include <stddef.h>
#include <stdint.h>

/* QMI-mapped PSRAM window (chip select 1) */
#define PSRAM_BASE 0x11000000u

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

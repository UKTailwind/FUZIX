/* 
 * nano-z80 SD-card transfer wrapper
 *
 */

#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <sdxfer.h>
#include <nano-z80.h>


// Place drives 64 Mb apart for now, starting with an offset of 128 Mb
static uint32_t offset[2] = {0x00100000, 0x00110000};

static void sd_set_addr(uint32_t address) {
    sd_sector0 = address & 0xff;
    sd_sector1 = (address >> 8) & 0xff;
    sd_sector2 = (address >> 16) & 0xff;
    sd_sector3 = (address >> 24) & 0xff;
    sd_set_sector_regs();
}

int nz80_sd_xfer(uint_fast8_t dev, bool is_read, uint32_t lba, uint8_t *dptr)
{
    // Set LBA address
    sd_set_addr(lba + offset[dev]);
    sd_ptr = dptr;
    if(is_read) sd_read_block();
    else sd_write_block();

} 



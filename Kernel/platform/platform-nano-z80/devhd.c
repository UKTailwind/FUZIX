/* 
 * nano-z80 HD driver
 *
 */

#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <devhd.h>
#include <nano-z80.h>

static int hd_transfer(bool is_read, uint8_t minor, uint8_t rawflag);

static uint8_t hd_map[16];
static uint8_t num_hd = 1;
static uint32_t secsize[16];

// Place drives 64 Mb apart for now, starting with an offset of 128 Mb
static uint32_t offset[2] = {0x00100000, 0x00110000};

static uint32_t sd_offset;

int hd_read(uint_fast8_t minor, uint_fast8_t rawflag, uint_fast8_t flag)
{
    flag;
    sd_offset = offset[minor];
    return hd_transfer(true, 'h', rawflag);
}

int hd_write(uint_fast8_t minor, uint_fast8_t rawflag, uint_fast8_t flag)
{
    flag;
    sd_offset = offset[minor];
    return hd_transfer(false, 'h', rawflag);
}

static void sd_set_addr(uint32_t address) {
    sd_sector0 = address & 0xff;
    sd_sector1 = (address >> 8) & 0xff;
    sd_sector2 = (address >> 16) & 0xff;
    sd_sector3 = (address >> 24) & 0xff;
    sd_set_sector_regs();
}


static int hd_transfer(bool is_read, uint8_t type, uint8_t rawflag)
{
    uint16_t ct = 0;
    uint8_t err;
    uint8_t hinti;
    uint16_t nblock;
    uint32_t block;

    irqflags_t irq;

    if(rawflag == 1) {
        if(d_blkoff(7))
            return -1;
        disk_map = udata.u_page;
        sd_ptr = (uint16_t)udata.u_base;
#ifdef SWAPDEV
    } else if (rawflag == 2) {		/* Swap device special */
        disk_map = swappage;
        sd_ptr = (uint16_t) udata.u_dptr;
#endif
    } else { /* rawflag == 0 */
        udata.u_nblock = 1;
        sd_ptr = (uint16_t) udata.u_dptr;
        disk_map = 0;
    }

    block = udata.u_block + sd_offset + 0x800; // 0x800 skips MBR etc. 
    nblock = udata.u_nblock;
    ct = 0;

    while (ct < nblock) {
        
        irq = di();
        sd_set_addr(block);
        if (is_read) {
            sd_read_block();
        } else {
            sd_write_block();
        }
        
        irqrestore(irq);

        sd_ptr += 512;
        ct++;
        block++;
    }
    return ct << 9;
}

int hd_open(uint_fast8_t minor, uint16_t flag)
{
    flag;
    if ((minor & 0x0F) >= num_hd ) {
        udata.u_error = ENODEV;
        return -1;
    }
    return 0;
}

int hd_close(uint_fast8_t minor)
{
    return 0;
}

void fdhd_init(void)
{
}

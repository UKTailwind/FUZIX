#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <devtty.h>
#include <tinyide.h>
#include <lb.h>
#include "ncr5380.h"

void map_init(void)
{
	/* TODO: set up CTC timer */
}

void device_init(void)
{
    scsi_init();
}

void pagemap_init(void)
{
    unsigned n = ramsize >> 5;
    kprintf("%u banks (%dK)\n", n, ramsize);
    while(--n > 1)
        pagemap_add(n);
}

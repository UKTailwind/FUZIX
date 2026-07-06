#include <kernel.h>
#include <devtty.h>
#include <printf.h>
#include <tinydisk.h>
#include "m4board.h"
#include "devm4board.h"
#include "../../dev/cpc/ds12885.h"
#include "plt_ch375.h"
#include <vt.h>
#include "devtty.h"

extern int8_t n_valid_maps;
extern uint8_t valid_maps_array[MAX_MAPS];
extern struct vt_switch ttysave[4];

/* TODO: probe banks */
void pagemap_init(void)
{
 /* 0xC2 is kernel, valid maps are validated and stored in cpcsme.s*/
	for (int8_t i = n_valid_maps - 1; i >= 0 ; i--) /*We go backwards as lower banks are faster to map*/
		pagemap_add(valid_maps_array[i]);
}

void map_init(void)
{

}

uint8_t plt_param(char *p)
{
 used(p);
 return 0;
}

void plt_copyright(void)
{
	kprintf("Amstrad CPC with standard memory expansion platform\nCopyright (c) 2024-2026 Antonio J. Casado Alias\n");
}

#if (defined CONFIG_USIFAC_SERIAL || defined CONFIG_USIFAC_CH376)
void usifac_flush(){
	char c;
	while ((usifctrl == 0xff)){
		c=usifdata; /*flush transmit buffer*/
		/*kprintf("%2x:",c);*/
	}
	/*kprintf("\n");*/
}
void usifac_init()
{
	irqflags_t irq = di();
	kprintf("Configuring Usifac\n");
	if (usifexists == 255){
		kprintf("Usifac not present\n");
		irqrestore(irq);
		return;
	}
#if (defined CONFIG_USIFAC_SERIAL && !(defined CONFIG_USIFAC_CH376))
	usifctrl = USIFAC_SET_115200B_COMMAND;
	usifac_flush();
#endif
#if (!(defined CONFIG_USIFAC_SERIAL) && (defined CONFIG_USIFAC_CH376))
	usifctrl = USIFAC_SET_9600B_COMMAND;
	usifac_flush();
	usifdata = 0x57;
	usifdata = 0xAB;
	usifdata = 0x02; /*CH375_CMD_SET_BAUDRATE*/
	usifdata = 0x03;
	usifdata = 0xFA;
	usifctrl = USIFAC_SET_1MBPS_COMMAND;
	usifac_flush();
#endif
	switch (usifgetbaud){
	case USIFAC_SET_1MBPS_COMMAND:
		kprintf("Usifac CH376 module serial comunication configured at 1MBPS\n");
		break;
	case USIFAC_SET_115200B_COMMAND:
		kprintf("Usifac serial port configured at 115200 baud\n");
		break;
	default:
		kprintf("Error configuring Usifac, baudcode:%u\n",usifgetbaud);
	}
	irqrestore(irq);
}
#endif



void device_init(void)
{
#ifdef CONFIG_M4BOARD
	uint8_t m4_open_err;
	m4_init();
	if (m4_present){
		kprintf("Registering M4 SD card raw acces device:\n");
        if (td_register(1, m4_sd_xfer, td_ioctl_none, 1) < 0)
			kprintf("FAIL\n");
		else{
			kprintf("Registering M4 SD card image file device:\n");
			m4_open_mode = FA_REALMODE | FA_READ;
			m4_open_err = m4_img_open();
			if (!m4_open_err){
				kputs("Found /FUZIX.IMG\n");
				td_register(1, m4_img_xfer, td_ioctl_none, 1);
			}
			else
				kprintf("Error %u opening FUZIX.IMG for read\n", m4_open_err);

		}
	}	
#endif
#ifdef CONFIG_RTC_DS12885
	ds12885_init();
	if (ds12885_present) 
		kprintf("DS12885 detected\n");
#endif
#if (defined CONFIG_USIFAC_SERIAL || defined CONFIG_USIFAC_CH376)
	usifac_init();
#endif
#if (defined CONFIG_ALBIREO || defined CONFIG_USIFAC_CH376)
	ch375_probe();
#endif

#ifdef CONFIG_TD_IDE
	ide_probe();
#endif
#ifdef CONFIG_NET
	sock_init();
#endif
devtty_init();

}
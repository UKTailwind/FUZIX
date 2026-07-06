#include <kernel.h>
#include <timer.h>
#include <kdata.h>
#include <printf.h>
#include <devtty.h>
#include <devinput.h>
#include "../../dev/cpc/ds12885.h"
#include "devm4board.h"

uint16_t swap_dev = 0xFFFF;
uaddr_t ramtop = PROGTOP;

#if ((defined CONFIG_M4BOARD) || (defined CONFIG_SYMBIFACE_RTC))
uint8_t plt_rtc_secs(void){
#ifdef CONFIG_SYMBIFACE_RTC
	if (ds12885_present)
		return sf_plt_rtc_secs();
	else 
#endif
#ifdef CONFIG_M4BOARD
		if (m4_present)
			return m4_plt_rtc_secs();
#endif
	return 0xff;
}
int plt_rtc_read(void){
#ifdef CONFIG_SYMBIFACE_RTC
	if (ds12885_present)
		return sf_plt_rtc_read();
	else
#endif
#ifdef CONFIG_M4BOARD		
		if (m4_present)
			return m4_plt_rtc_read();
#endif
	udata.u_error = EOPNOTSUPP;
	return -1;
}
int plt_rtc_write(void){
#ifdef CONFIG_SYMBIFACE_RTC
	if (ds12885_present)
		return sf_plt_rtc_write();
#endif
	udata.u_error = EOPNOTSUPP;
	return -1;
}
#endif

void plt_idle(void)
{
 __asm
  halt
 __endasm;
}

uint8_t timer_wait;

void plt_interrupt(void)
{
	tty_pollirq();
#ifdef CONFIG_USIFAC_SERIAL
	tty_pollirq_usifac();
#endif
#ifdef CONFIG_NET_WIZNET
	w5x00_poll();
#endif
	timer_interrupt();
	poll_input();
	if (timer_wait)
		wakeup(&timer_interrupt);
#ifdef CONFIG_FDC765
	devfd_spindown();
#endif
}

/*
 *	So that we don't suck in a library routine we can't use from
 *	the runtime
 */

size_t strlen(const char *p)
{
  size_t len = 0;
  while(*p++)
    len++;
  return len;
}

/* This points to the last buffer in the disk buffers. There must be at least
   four buffers to avoid deadlocks. */
#ifdef CONFIG_DYNAMIC_BUFPOOL
struct blkbuf *bufpool_end = bufpool + NBUFS;
#endif
/*
 *	We pack discard into the memory image is if it were just normal
 *	code but place it at the end after the buffers. When we finish up
 *	booting we turn everything from the buffer pool to the start of
 *	common space into buffers.
 */
void plt_discard(void)
{
#ifdef CONFIG_DYNAMIC_BUFPOOL
	uint16_t discard_size = PROGTOP - (uint16_t)bufpool_end;
	bufptr bp = bufpool_end;

	discard_size /= sizeof(struct blkbuf);

	kprintf("%d buffers added\n", discard_size);

	bufpool_end += discard_size;

	memset( bp, 0, discard_size * sizeof(struct blkbuf) );

	for( bp = bufpool + NBUFS; bp < bufpool_end; ++bp ){
		bp->bf_dev = NO_DEVICE;
		bp->bf_busy = BF_FREE;
	}
#endif
}

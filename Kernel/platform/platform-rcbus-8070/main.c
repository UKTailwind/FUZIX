#include <kernel.h>
#include <timer.h>
#include <kdata.h>
#include <printf.h>
#include <devtty.h>

uint8_t kernel_flag = 1;
uint8_t need_resched;
uint16_t swap_dev = 0xFFFF;

void plt_idle(void)
{
	irqflags_t flags = di();
	tty_poll();
	irqrestore(flags);
}

void do_beep(void)
{
}

#define timer *((volatile uint8_t *)0xFE0F)

void plt_interrupt(void)
{
	static uint8_t count;
	uint8_t r;

	tty_poll();
	r = timer;
	if (!(r & 0x80)) {
		count += r & 3;
		timer = 0x00;
		timer = 0x80;
		/* Now handle any ticks we've accumulated */
		if (count >= 5) {
			timer_interrupt();
			count -= 5;
		}
	}
}

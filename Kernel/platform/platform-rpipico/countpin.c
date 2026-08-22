/*
 *	The counting inputs: SETPIN FIN / CIN / PER on GP4-GP7.
 *	PLAN-count.md (Applications/mmb2c) is the design; the mechanism
 *	is MMBasic's, copied cell for cell from PicoMite External.c /
 *	PicoMite.c and cited below by line.
 *
 *	This inverts the port's usual pin split.  Every other SETPIN mode
 *	is a register store from userland; counting needs an interrupt,
 *	userland cannot own one, and the kernel cannot call program code -
 *	so the counters live here and userland reads them by ioctl
 *	(~1.5us, against a per-STATEMENT cost everywhere else).
 *
 *	Per pin, MMBasic keeps three cells (External.c:129,133):
 *
 *	  count  int64: CIN/FIN, edges seen; PER, elapsed MILLISECONDS
 *	  value  int32: the last completed gate's latched result
 *	  left   int32: the gate countdown - FIN ms (ticked), PER cycles
 *	         (decremented per edge)
 *
 *	FIN: edge -> count++; a 1ms timer decrements left, at zero latches
 *	value=count, zeroes count, reloads (UPDATE_FREQ_INPUT,
 *	PicoMite.c:2220-2226).  PIN() = value*1000/gate, scaled in
 *	userland.
 *	PER: the roles invert - the 1ms timer does count++
 *	(UPDATE_PER_INPUT, PicoMite.c:2228-2230), the EDGE decrements left
 *	and latches (TM_EXTI_Handler_1, External.c:6025-6033).  PIN() =
 *	value/cycles ms, scaled in userland.
 *	CIN: edge -> count++, and PIN() reads the LIVE count; Pin(n)=v
 *	stores ANY value, not just 0 (External.c:603-613).
 *
 *	PLACEMENT.  The two callbacks below are the hot path and must be
 *	RAM-resident: this kernel executes from flash through the same QMI
 *	core1's scanout streams PSRAM through, and a priority-0 IRQ at up
 *	to ~100kHz fetching through XIP would both jitter and add
 *	contention where flecking has been fought before.  The excludes
 *	file names this file's COLD functions (ioctl, reset) into flash
 *	and leaves the callbacks unnamed, which is the port's native
 *	spelling of PicoMite's __not_in_flash_func.  The SDK's dispatcher,
 *	gpio_default_irq_handler, gets the same treatment by a pre-link
 *	section rename - see relocate_gpio_irq_to_ram.cmake, ported from
 *	PicoMite, and the note beside the gpio.c line in
 *	default_text_excludes.incl.
 *
 *	PRIORITY 0, as MMBasic sets it (External.c:1012).  The SDK default
 *	is 0x80, so this becomes the machine's one priority-0 IRQ: a
 *	~30-instruction handler may preempt anything - including the audio
 *	synth's DMA-IRQ work - and nothing may delay an edge.
 *
 *	CONCURRENCY.  Kernel is non-preemptive, so the ioctl bodies are
 *	atomic against other processes; against the two IRQ contexts the
 *	64-bit cells are copied under save_and_disable_interrupts().
 *	MMBasic reads its volatile int64 unguarded and can tear a read
 *	each time the count crosses 2^32; ours sits in an ioctl already,
 *	so the guard is free.  A bug fix, not a divergence - nothing
 *	observable changes.  The PER timer-vs-edge race on `count` is
 *	MMBasic's own structure (same two contexts, same priorities) and
 *	is left exactly as the reference has it.
 */

#include <kernel.h>
#include <kdata.h>
#include "config.h"
#include "picosdk.h"
#include "pico_ioctl.h"
#include "pinlock.h"
#include "countpin.h"
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/sync.h>
#include <pico/time.h>

#define CNT_FIRST	4		/* GP4..GP7 = MMBasic INT1..INT4 */
#define CNT_NPINS	4

#define CNT_OFF		0
#define CNT_FIN		1
#define CNT_CIN		2
#define CNT_PER		3

struct cntpin {
	uint8_t mode;
	int32_t init;			/* FIN gate ms / PER cycles / CIN option */
	volatile int32_t left;
	volatile int32_t value;
	volatile int64_t count;
};

static struct cntpin cnt[CNT_NPINS];
static repeating_timer_t cnt_timer;
static uint8_t cnt_timer_on;
static uint8_t cnt_cb_set;

/*
 *	The edge.  TM_EXTI_Handler_x (External.c:6023-6041) minus the
 *	CFunction hook this machine does not have.  RAM-resident by
 *	omission from the excludes file.
 */
static void cnt_gpio_cb(uint gpio, uint32_t events)
{
	struct cntpin *c;

	(void)events;
	if (gpio - CNT_FIRST >= CNT_NPINS)
		return;
	c = &cnt[gpio - CNT_FIRST];
	if (c->mode == CNT_PER) {
		if (--c->left <= 0) {
			c->value = (int32_t)c->count;
			c->left = c->init;
			c->count = 0;
		}
	} else
		c->count++;
}

/*
 *	The millisecond.  PicoMite's timer_callback lines 2526-2535 for
 *	the two modes that need time; runs only while one of them is
 *	configured.  RAM-resident by omission, like the edge.
 */
static bool cnt_tick_cb(repeating_timer_t *rt)
{
	unsigned i;

	(void)rt;
	for (i = 0; i < CNT_NPINS; i++) {
		struct cntpin *c = &cnt[i];
		if (c->mode == CNT_FIN) {
			if (--c->left <= 0) {
				c->value = (int32_t)c->count;
				c->count = 0;
				c->left = c->init;
			}
		} else if (c->mode == CNT_PER)
			c->count++;
	}
	return true;
}

/*	Start the 1ms timer when the first FIN/PER pin arrives, stop it
 *	when the last leaves - interrupts exist only while a SETPIN wants
 *	them.  The alarm pool is proven here (the 200Hz system tick uses
 *	it, devices.c) and SIZED here: PICO_TIME_DEFAULT_ALARM_POOL_MAX_
 *	TIMERS is 2, the tick plus exactly this.  Returns 0 if the pool
 *	refused, so a FIN/PER config can fail honestly instead of gating
 *	at zero forever. */
static int cnt_timer_check(void)
{
	unsigned i, need = 0;

	for (i = 0; i < CNT_NPINS; i++)
		if (cnt[i].mode == CNT_FIN || cnt[i].mode == CNT_PER)
			need = 1;
	if (need && !cnt_timer_on) {
		cnt_timer_on = add_repeating_timer_us(-1000, cnt_tick_cb,
						      NULL, &cnt_timer);
		return cnt_timer_on;
	}
	if (!need && cnt_timer_on) {
		cancel_repeating_timer(&cnt_timer);
		cnt_timer_on = 0;
	}
	return 1;
}

void countpin_reset(uint_fast8_t gpio)
{
	struct cntpin *c;
	uint32_t irq;

	if ((uint_fast8_t)(gpio - CNT_FIRST) >= CNT_NPINS)
		return;
	c = &cnt[gpio - CNT_FIRST];
	/* Both edges off whatever was on - ExtCfg's head (External.c:
	   790-821).  gpio_init() does NOT touch IO_BANK0's interrupt
	   enables, which is why pinlock's reset calls this FIRST. */
	gpio_set_irq_enabled(gpio,
			     GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
	irq = save_and_disable_interrupts();
	c->mode = CNT_OFF;
	c->count = 0;
	c->value = 0;
	c->init = c->left = 0;
	restore_interrupts(irq);
	cnt_timer_check();
}

static int cnt_config(uint_fast8_t gpio, uint8_t mode, int32_t arg)
{
	struct cntpin *c = &cnt[gpio - CNT_FIRST];
	uint32_t edge = GPIO_IRQ_EDGE_RISE;
	uint32_t irq;

	countpin_reset(gpio);

	/* The pin itself: input, buffer on (the RP2350 pad trap devgpio.c
	   documents), Schmitt trigger as every MMBasic counting input
	   gets (External.c:1030), pulls cleared then applied fresh
	   (External.c:831, 1008-1011). */
	gpio_init(gpio);
	gpio_set_dir(gpio, GPIO_IN);
	gpio_set_input_enabled(gpio, true);
	gpio_set_input_hysteresis_enabled(gpio, true);
	gpio_disable_pulls(gpio);

	/* MMBasic's shared option logic, replicated as-is INCLUDING its
	   quirk: the edge tests are CIN-only but the pull tests see every
	   mode's third argument, so FIN with a 1/2/4/5 ms gate - and PER
	   at its DEFAULT of 1 cycle - collect a pull too (External.c:
	   1003-1011).  For PER that is documented behaviour by now. */
	if (mode == CNT_CIN && arg == 2)
		edge = GPIO_IRQ_EDGE_FALL;
	if (mode == CNT_CIN && arg >= 3)
		edge = GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE;
	if (arg == 1 || arg == 4)
		gpio_pull_down(gpio);
	if (arg == 2 || arg == 5)
		gpio_pull_up(gpio);

	irq = save_and_disable_interrupts();
	c->count = 0;
	c->value = 0;
	c->init = c->left = arg;
	c->mode = mode;
	restore_interrupts(irq);

	irq_set_priority(IO_IRQ_BANK0, 0);
	if (!cnt_cb_set) {
		gpio_set_irq_enabled_with_callback(gpio, edge, true,
						   cnt_gpio_cb);
		cnt_cb_set = 1;
	} else
		gpio_set_irq_enabled(gpio, edge, true);
	if (!cnt_timer_check()) {
		/* The pool refused the gate timer (it is sized for exactly
		   this, so only a future third consumer can get here).
		   Undo rather than count a gate that never closes. */
		countpin_reset(gpio);
		udata.u_error = EIO;
		return -1;
	}
	return 0;
}

int countpin_ioctl(uarg_t request, char *data)
{
	struct cntreq cr;
	struct cntpin *c;
	uint32_t irq;

	if (uget(data, &cr, sizeof(struct cntreq)) == -1)
		return -1;

	if ((uint_fast8_t)(cr.pin - CNT_FIRST) >= CNT_NPINS) {
		udata.u_error = EINVAL;
		return -1;
	}
#ifdef CONFIG_PC3_PINLOCK
	/* The one pin mode where the kernel holds state, so the advisory
	   claim is enforced: no claim, no counter. */
	if (pinlock_owner(PLK_PIN, cr.pin) != udata.u_ptab->p_pid) {
		udata.u_error = EPERM;
		return -1;
	}
#endif
	c = &cnt[cr.pin - CNT_FIRST];

	switch (request) {
	case GPIOC_CNT_FIN:
		/* Ranges are MMBasic's getint() bounds, External.c:
		   1960-1977. */
		if (cr.arg < 1 || cr.arg > 100000)
			break;
		return cnt_config(cr.pin, CNT_FIN, cr.arg);
	case GPIOC_CNT_CIN:
		if (cr.arg < 1 || cr.arg > 10)
			break;
		return cnt_config(cr.pin, CNT_CIN, cr.arg);
	case GPIOC_CNT_PER:
		if (cr.arg < 1 || cr.arg > 10000)
			break;
		return cnt_config(cr.pin, CNT_PER, cr.arg);
	case GPIOC_CNT_READ:
		irq = save_and_disable_interrupts();
		if (c->mode == CNT_CIN)
			cr.val = c->count;
		else
			cr.val = c->value;
		restore_interrupts(irq);
		if (c->mode == CNT_OFF)
			break;
		return uput(&cr, data, sizeof(struct cntreq));
	case GPIOC_CNT_SET:
		/* CIN only: everything else is "not an output", raised
		   with MMBasic's words in userland before the call ever
		   gets here - this is the backstop. */
		if (c->mode != CNT_CIN)
			break;
		irq = save_and_disable_interrupts();
		c->count = cr.val;
		restore_interrupts(irq);
		return 0;
	case GPIOC_CNT_OFF:
		countpin_reset(cr.pin);
		gpio_disable_pulls(cr.pin);
		return 0;
	}
	udata.u_error = EINVAL;
	return -1;
}

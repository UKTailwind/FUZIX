#ifndef MMB_INT_H
#define MMB_INT_H
/*
 *	MMBasic's software interrupts - the pin half.
 *
 *	The load-bearing fact, and the reason this is a header of statics
 *	rather than anything cleverer: MMBASIC'S INTERRUPTS ARE NOT
 *	INTERRUPTS.  The whole facility is a poll.  No BASIC ever runs
 *	asynchronously there either: the interpreter calls
 *	check_interrupt() after EVERY statement (MMBasic.c:1878), so the
 *	latency guarantee is one statement and a statement is atomic.
 *	Pin "interrupts" are level compares against the level at the
 *	previous check (MM_Misc.c:10153) - there is no GPIO IRQ anywhere
 *	in MMBasic, and a pulse shorter than a statement is missed.
 *
 *	So this is the same algorithm with the level read from a register
 *	instead of MMBasic's PinRead, and it is deliberately NOT a Unix
 *	signal.  Signals were considered and rejected on three grounds:
 *	the kernel is non-preemptive and delivers at syscall boundaries,
 *	so a compiled compute loop would never see one; the runtime's
 *	string scratch stack and queues are not reentrant, while a
 *	statement boundary is exactly where everything is quiescent; and
 *	MMBasic is the proven implementation of this facility AS a poll.
 *	See PLAN-interrupts.md.
 *
 *	Per process, which is a sentence MMBasic could never write: every
 *	BASIC program owns its own table, and two can each run their own.
 *
 *	Costs nothing when unused.  The translator emits the poll only for
 *	a program that arms something, and cc1 emits nothing for a static
 *	that nothing names - the mmb_gpio.h bargain.
 */

#include "mmb_runtime.h"
#include "mmb_gpio.h"

/*	MMBasic's own limit is 10 pin interrupts (MAXINTERRUPTS,
 *	configuration.h); the PC3's I/O header has 22 claimable pins, so
 *	ten is not the binding constraint and matching MMBasic costs
 *	nothing. */
#define MM_INT_NPIN	10

typedef void (*mm_int_fn)(void);

static struct {
	unsigned char pin;
	unsigned char edge;		/* MMG_PIN_INTH / INTL / INTB */
	signed char last;		/* the level at the previous check */
	mm_int_fn fn;
} mm_ipins[MM_INT_NPIN];

static int mm_ipin_n;

/*	Non-zero when anything is armed.  The per-statement poll site is
 *	"if (__mm_int_armed) mm_int_poll();", so a program that has armed
 *	nothing yet pays one global load and a not-taken branch. */
static int __mm_int_armed;

/*	Inside a handler.  MMBasic's InterruptReturn gate (MM_Misc.c:
 *	10242): interrupts NEVER nest.  A handler's own statements still
 *	carry poll sites - they are ordinary generated statements - and
 *	this is what makes them no-ops. */
static int __mm_in_int;

/*
 *	GotAnInterrupt, minus the trampoline.
 *
 *	A handler is a SUB and ends with END SUB - there is no IRETURN to
 *	write, and that is MMBasic's behaviour, not a simplification.  For
 *	a SUB target MMBasic fakes a GOSUB whose RETURN ADDRESS is a
 *	synthetic two-token IRETURN it builds itself (rti[],
 *	MM_Misc.c:10205-10210), so END SUB returns onto that dummy and the
 *	interrupt return happens implicitly.  Written IRETURN only exists
 *	for the legacy label and line-number targets, which do not survive
 *	translation anyway (see int_handler in the translator).
 *
 *	So a compiler needs none of the trampoline: the handler is a
 *	function, call and return ARE the GOSUB and the synthetic IRETURN,
 *	and C locals replace the g_LocalIndex bookkeeping.  What does have
 *	to be copied is the error-state save-clear-restore around it.
 */
MMG_FN void mm_int_fire(mm_int_fn fn)
{
	mm_int_err_push();
	__mm_in_int = 1;
	fn();
	__mm_in_int = 0;
	mm_int_err_pop();
}

/*
 *	SETPIN pin, INTH|INTL|INTB, handler.
 *
 *	Re-arming a pin already in the table replaces it, which is what
 *	MMBasic does - the pin has one interrupt, not a list.
 *
 *	`last` is seeded from the pin HERE, as External.c:2050 does, and
 *	that is not a detail: without it the first poll compares against
 *	zero and a pin already sitting high fires an edge that never
 *	happened.
 */
MMG_FN void mmi_setpin_int(MMINTEGER pin, MMINTEGER edge, mm_int_fn fn)
{
	int i;

	mmg_setpin(pin, edge);		/* claims, configures, records mode */
	if (mmg_mode[pin] != (unsigned char)edge)
		return;			/* it refused, and has said so */

	for (i = 0; i < mm_ipin_n; i++)
		if (mm_ipins[i].pin == (unsigned char)pin)
			break;
	if (i == mm_ipin_n) {
		if (mm_ipin_n >= MM_INT_NPIN) {
			mm_error("Too many interrupts");
			return;
		}
		mm_ipin_n++;
		__mm_int_armed++;
	}
	mm_ipins[i].pin = (unsigned char)pin;
	mm_ipins[i].edge = (unsigned char)edge;
	mm_ipins[i].last = (signed char)pc3_pin_get((int)pin);
	mm_ipins[i].fn = fn;
}

/*	SETPIN pin, OFF on a pin that has an interrupt: disarm it as well
 *	as resetting the pin.  Emitted instead of plain mmg_setpin only by
 *	a program that uses interrupts at all. */
MMG_FN void mmi_setpin_off(MMINTEGER pin)
{
	int i, j;

	for (i = 0; i < mm_ipin_n; i++) {
		if (mm_ipins[i].pin == (unsigned char)pin) {
			for (j = i + 1; j < mm_ipin_n; j++)
				mm_ipins[j - 1] = mm_ipins[j];
			mm_ipin_n--;
			__mm_int_armed--;
			break;
		}
	}
	mmg_setpin(pin, MMG_PIN_OFF);
}

/*
 *	check_interrupt + checkdetailinterrupts, restricted to pins.
 *
 *	ONE dispatch per call, as MMBasic does: the first hit wins and the
 *	next statement boundary picks up the next one.  The scan order is
 *	the priority scheme; when ticks and keys arrive they go after the
 *	pins here, which is checkdetailinterrupts' own order.
 *
 *	The comparisons are MMBasic's exactly (MM_Misc.c:10153):
 *	INTH is v > last, INTL is v < last, INTB is any change, and `last`
 *	is updated whether or not anything fires.
 */
MMG_FN void mm_int_poll(void)
{
	int i, v, last;

	if (__mm_in_int)
		return;
	for (i = 0; i < mm_ipin_n; i++) {
		v = pc3_pin_get((int)mm_ipins[i].pin);
		last = mm_ipins[i].last;
		if (v == last)
			continue;
		mm_ipins[i].last = (signed char)v;
		if (mm_ipins[i].edge == MMG_PIN_INTB
		    || (mm_ipins[i].edge == MMG_PIN_INTH && v > last)
		    || (mm_ipins[i].edge == MMG_PIN_INTL && v < last)) {
			mm_int_fire(mm_ipins[i].fn);
			return;
		}
	}
}

#endif /* MMB_INT_H */

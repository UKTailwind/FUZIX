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

/*	NBRSETTICKS - MMBasic's four, ids 1 to 4. */
#define MM_INT_NTICK	4

typedef void (*mm_int_fn)(void);

/*	SETTICK's clock.  On the board the microsecond counter is three
 *	loads (pc3_us64); everywhere else it is the runtime's, so the same
 *	code runs under the gates with a real clock behind it and a tick
 *	test means something before it reaches hardware. */
#if defined(MM_PC3) || defined(__FUZIX__)
#define MMI_US()	pc3_us64()
#else
#define MMI_US()	((long long)mm_us())
#endif

static struct {
	unsigned char pin;
	unsigned char edge;		/* MMG_PIN_INTH / INTL / INTB */
	signed char last;		/* the level at the previous check */
	mm_int_fn fn;
} mm_ipins[MM_INT_NPIN];

static int mm_ipin_n;

/*	The four SETTICK timers.  MMBasic counts milliseconds in an ISR
 *	and fires when the count passes the period; this holds a DEADLINE
 *	in microseconds instead, which needs no interrupt and no counter -
 *	the poll is already happening, so asking "is it time yet" is one
 *	comparison.
 *
 *	`left` is what PAUSE freezes: MMBasic stops incrementing the
 *	counter, which leaves the time-to-go where it stands, and RESUME
 *	starts it again from there.  A deadline has to be rebuilt from the
 *	new now, so the remainder is what gets stored. */
static struct {
	unsigned char armed;		/* a handler is set */
	unsigned char active;		/* not PAUSEd */
	long long period;		/* us */
	long long due;			/* us, absolute */
	long long left;			/* us to go, while paused */
	mm_int_fn fn;
} mm_tick[MM_INT_NTICK];

/*	How many ticks are armed, so the poll can skip reading the clock
 *	entirely for a program that only uses pins. */
static int mm_ntick_armed;

/*	ON KEY, both forms.
 *
 *	The any-key form fires while a key is WAITING and leaves it there
 *	for INKEY$ inside the handler; the specific form fires on one code
 *	and EATS it.  That asymmetry is MMBasic's (PicoMite.c:932-935,
 *	where the console interrupt consumes the selected key and lets
 *	every other one through) and it is the whole point of the pair.
 */
static mm_int_fn mm_key_any_fn;
static mm_int_fn mm_key_sel_fn;
static int mm_key_sel;			/* the code the specific form wants */

/*	Looking at the console is a SYSCALL - termios and a read - and a
 *	poll site runs after every statement, so checking each time would
 *	cost more than most statements do.  It is checked at most once per
 *	MM_INT_CON_US instead, timed off the clock that is already here.
 *
 *	5 ms is the kernel's own tick and far below anything a person can
 *	type or notice, so the worst added latency is invisible; it is
 *	named as a divergence anyway, because it IS one - MMBasic looks
 *	every statement. */
#define MM_INT_CON_US	5000
static long long mm_key_next;		/* earliest us at which to look */

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
MMG_FN void mmi_setpin_int(MMINTEGER pin, MMINTEGER edge, mm_int_fn fn,
			   MMINTEGER pull)
{
	int i;

	mmg_setpin(pin, edge, pull);	/* claims, configures, records mode */
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
	mmg_setpin(pin, MMG_PIN_OFF, 0);
}

/*
 *	SETTICK period, handler [, id]   -- period in MILLISECONDS
 *	SETTICK 0, 0 [, id]              -- off
 *
 *	ids are 1-4 and out-of-range is MMBasic's error.  Arming sets the
 *	first deadline one whole period away, as MMBasic's TickTimer = 0
 *	does.
 */
MMG_FN void mmi_settick(MMINTEGER ms, mm_int_fn fn, MMINTEGER id)
{
	int i = (int)id - 1;

	if (i < 0 || i >= MM_INT_NTICK) {
		mm_error("Invalid tick number");
		return;
	}
	if (ms <= 0) {			/* SETTICK 0, 0 - off */
		if (mm_tick[i].armed) {
			__mm_int_armed--;
			mm_ntick_armed--;
		}
		mm_tick[i].armed = 0;
		mm_tick[i].active = 0;
		mm_tick[i].fn = 0;
		return;
	}
	if (!mm_tick[i].armed) {
		__mm_int_armed++;
		mm_ntick_armed++;
	}
	mm_tick[i].armed = 1;
	mm_tick[i].active = 1;
	mm_tick[i].period = (long long)ms * 1000;
	mm_tick[i].due = MMI_US() + mm_tick[i].period;
	mm_tick[i].left = 0;
	mm_tick[i].fn = fn;
}

/*	SETTICK PAUSE / RESUME [, id].  MMBasic freezes the count where it
 *	stands and starts it again from there; a deadline has to be
 *	rebuilt, so the time-to-go is what is kept. */
MMG_FN void mmi_settick_pause(MMINTEGER id, MMINTEGER on)
{
	int i = (int)id - 1;

	if (i < 0 || i >= MM_INT_NTICK) {
		mm_error("Invalid tick number");
		return;
	}
	if (!mm_tick[i].armed)
		return;
	if (!on) {			/* PAUSE */
		if (mm_tick[i].active) {
			mm_tick[i].left = mm_tick[i].due - MMI_US();
			if (mm_tick[i].left < 0)
				mm_tick[i].left = 0;
			mm_tick[i].active = 0;
		}
	} else {			/* RESUME */
		if (!mm_tick[i].active) {
			mm_tick[i].due = MMI_US() + mm_tick[i].left;
			mm_tick[i].active = 1;
		}
	}
}

/*	ON KEY handler   /   ON KEY 0   (off) */
MMG_FN void mmi_onkey_any(mm_int_fn fn)
{
	if (fn && !mm_key_any_fn)
		__mm_int_armed++;
	else if (!fn && mm_key_any_fn)
		__mm_int_armed--;
	mm_key_any_fn = fn;
}

/*	ON KEY code, handler   /   ON KEY code, 0   /   ON KEY 0, ... (off)
 *
 *	MMBasic takes 0-255 and treats a zero code, or a zero handler, as
 *	turning it off. */
MMG_FN void mmi_onkey_sel(MMINTEGER code, mm_int_fn fn)
{
	if (code < 0 || code > 255) {
		mm_error("Invalid key code");
		return;
	}
	if (code == 0)
		fn = 0;
	if (fn && !mm_key_sel_fn)
		__mm_int_armed++;
	else if (!fn && mm_key_sel_fn)
		__mm_int_armed--;
	mm_key_sel_fn = fn;
	mm_key_sel = (int)code;
}

/*
 *	check_interrupt + checkdetailinterrupts: keys, pins, then ticks.
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

	/*	Keys FIRST, and the specific form before the any-key one -
	 *	checkdetailinterrupts' order (MM_Misc.c:9892-9903). */
	if (mm_key_any_fn || mm_key_sel_fn) {
		long long now = MMI_US();

		if (now >= mm_key_next) {
			int c;

			mm_key_next = now + MM_INT_CON_US;
			c = (int)mm_key_peek();
			if (c) {
				if (mm_key_sel_fn && c == mm_key_sel) {
					/*	The selected key is EATEN -
					 *	it never reaches INKEY$,
					 *	which is what tells the two
					 *	forms apart. */
					mm_key_drop();
					mm_int_fire(mm_key_sel_fn);
					return;
				}
				if (mm_key_any_fn) {
					/*	Left where it is: the
					 *	handler reads it with
					 *	INKEY$, and if it does not,
					 *	this fires again - which is
					 *	MMBasic's behaviour, not an
					 *	oversight. */
					mm_int_fire(mm_key_any_fn);
					return;
				}
			}
		}
	}

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

	/*	Ticks last, which is checkdetailinterrupts' own order
	 *	(MM_Misc.c:10170) - so a pin edge and a tick due at the
	 *	same moment dispatch the pin first and the tick at the
	 *	next statement.
	 *
	 *	The clock is read once for all four, and only if one is
	 *	armed: a program with pins but no ticks pays nothing here.
	 */
	if (mm_ntick_armed) {
		long long now = MMI_US();

		for (i = 0; i < MM_INT_NTICK; i++) {
			if (!mm_tick[i].active || now < mm_tick[i].due)
				continue;
			/*	Catch up by whole periods, which KEEPS THE
			 *	PHASE and drops the firings that were
			 *	missed rather than queueing them - a
			 *	handler that runs longer than its own
			 *	period must not spiral.  MMBasic's
			 *	"while (TickTimer > TickPeriod)
			 *	TickTimer -= TickPeriod" is the same
			 *	arithmetic from the other end. */
			do {
				mm_tick[i].due += mm_tick[i].period;
			} while (mm_tick[i].due <= now);
			mm_int_fire(mm_tick[i].fn);
			return;
		}
	}
}

#endif /* MMB_INT_H */

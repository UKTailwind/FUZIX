#ifndef MMB_PULSE_H
#define MMB_PULSE_H
/*
 *	PULSE pin, width_ms
 *
 *	MMBasic's cmd_pulse (misc/External.c:2400), and the first thing to
 *	know about it is that it INVERTS.  It does not drive the pin high
 *	and then low: it flips whatever the pin is, waits, and flips back,
 *	so a pulse on a pin sitting high is a low-going pulse.  Every edge
 *	MMBasic emits is a LATINV.
 *
 *	The second thing is the three-millisecond split, which is not an
 *	optimisation but two different behaviours:
 *
 *	    under 3 ms   done here and now, the statement blocks
 *	    3 ms or more returns AT ONCE and the pin flips back later
 *
 *	Short pulses are the common case by far - a trigger for an
 *	ultrasonic ranger is 10 us, a servo frame 1 to 2 ms - and those
 *	are exact, spun out against the same microsecond clock MMBasic
 *	uses.  The long ones are for blinking something while the program
 *	gets on with its work.
 *
 *	WHERE OURS DIFFERS, and it is worth being plain about it: MMBasic
 *	ends a long pulse from a hardware timer interrupt, so it ends on
 *	time whatever the program is doing.  Fuzix has no sub-second
 *	interval timer - no setitimer, and alarm() counts whole seconds -
 *	so there is nothing to hang an ISR on.  A long pulse here ends at
 *	the next moment the program can be asked: any PAUSE, any later
 *	PULSE, or any statement boundary if the program also uses
 *	interrupts.  A program that starts a 500 ms pulse and then
 *	computes solidly for a second without pausing will hold the pin
 *	for the full second.
 *
 *	A PAUSE is not a coarse sampler here, though: mmb_wait.h asks
 *	mmg_pulse_slice_us how long there is to go and waits exactly that
 *	much, so a pulse that ends during a PAUSE ends on time rather than
 *	at the next round number.
 *
 *	In practice a program that wants a long pulse is a program with a
 *	PAUSE in its loop - that is why it wanted the non-blocking form -
 *	so the common case is right; the uncommon one is late rather than
 *	wrong, and it says so here rather than in a bug report.
 */

#include "mmb_runtime.h"
#include "mmb_gpio.h"

/*	MMBasic's NBR_PULSE_SLOTS (External.h:52). */
#define MM_PULSE_SLOTS	5

static struct {
	unsigned char pin;
	unsigned char busy;
	long long due;			/* us, absolute */
} mm_pulse[MM_PULSE_SLOTS];

static int mm_pulse_n;			/* slots in use */

#if defined(MM_PC3) || defined(__FUZIX__)
#define MMP_US()	pc3_us64()
#else
#define MMP_US()	((long long)mm_us())
#endif

/*	End any pulse whose time is up.  Called from PAUSE (mmb_wait.h),
 *	from the interrupt poll if the program has one, and at the top of
 *	PULSE itself. */
MMG_FN void mmg_pulse_service(void)
{
	long long now = MMP_US();
	int i;

	for (i = 0; i < MM_PULSE_SLOTS; i++)
		if (mm_pulse[i].busy && now >= mm_pulse[i].due) {
			pc3_pin_toggle((int)mm_pulse[i].pin);
			mm_pulse[i].busy = 0;
			mm_pulse_n--;
		}
}

/*	Microseconds until the earliest pulse is due, or 0 when none is
 *	running.  This is what lets a PAUSE sleep right up to the end of a
 *	pulse and wake for it, rather than sampling on a fixed beat and
 *	overshooting by up to a slice. */
MMG_FN long long mmg_pulse_slice_us(void)
{
	long long now, s = 0;
	int i;

	if (mm_pulse_n == 0)
		return 0;
	now = MMP_US();
	for (i = 0; i < MM_PULSE_SLOTS; i++)
		if (mm_pulse[i].busy) {
			long long left = mm_pulse[i].due - now;

			if (left < 1)
				left = 1;
			if (s == 0 || left < s)
				s = left;
		}
	return s;
}

MMG_FN void mmg_pulse(MMINTEGER pin, MMFLOAT ms)
{
	long long x, y;
	int i;

	if (pin < 0 || pin >= MM_GPIO_NPINS) {
		mm_error("Invalid pin");
		return;
	}
	if (mmg_mode[pin] != MMG_PIN_DOUT) {
		mm_error("Pin is not an output");
		return;
	}
	if (ms < 0) {
		mm_error("Number out of bounds");
		return;
	}
	mmg_pulse_service();		/* anything already due, before this */

	/*	Whole milliseconds and the fraction as microseconds, split
	 *	exactly as MMBasic splits it - the 3 ms test below is against
	 *	the WHOLE part, so 2.999 blocks and 3.0 does not. */
	x = (long long)ms;
	y = (long long)((ms - (MMFLOAT)x) * 1000.0);

	/*	Already pulsing on this pin?  MMBasic retimes it rather than
	 *	starting a second one, and a width of zero ends it now. */
	for (i = 0; i < MM_PULSE_SLOTS; i++)
		if (mm_pulse[i].busy && mm_pulse[i].pin == (unsigned char)pin) {
			if (x == 0) {
				pc3_pin_toggle((int)pin);
				mm_pulse[i].busy = 0;
				mm_pulse_n--;
			} else {
				mm_pulse[i].due = MMP_US() + x * 1000 + y;
			}
			return;
		}

	if (x == 0 && y == 0)
		return;			/* a zero pulse is silently nothing */

	if (x < 3) {
		/*	Here and now.  Both edges are inversions and the wait
		 *	between them is a spin: this is at most 3 ms and the
		 *	whole point is that the width is the width, so giving
		 *	up the processor for it would be worse than useless. */
		long long end;

		pc3_pin_toggle((int)pin);
		end = MMP_US() + x * 1000 + y;
		while (MMP_US() < end)
			;
		pc3_pin_toggle((int)pin);
		return;
	}

	for (i = 0; i < MM_PULSE_SLOTS; i++)
		if (!mm_pulse[i].busy)
			break;
	if (i >= MM_PULSE_SLOTS) {
		mm_error("Too many concurrent PULSE commands");
		return;
	}
	pc3_pin_toggle((int)pin);	/* the starting edge, now */
	mm_pulse[i].pin = (unsigned char)pin;
	mm_pulse[i].due = MMP_US() + x * 1000 + y;
	mm_pulse[i].busy = 1;
	mm_pulse_n++;
}

#endif /* MMB_PULSE_H */

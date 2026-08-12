#ifndef MMB_WAIT_H
#define MMB_WAIT_H
/*
 *	PAUSE, for a program that has something to service while it waits.
 *
 *	MMBasic's cmd_pause (MM_Misc.c:640) calls check_interrupt() every
 *	time round its busy loop, so a SETTICK handler fires during a
 *	PAUSE.  That matters more than it sounds: a program that arms a
 *	tick usually has a main loop of little but PAUSE, so a PAUSE that
 *	ignored the tick would mean the handler never ran AT ALL.
 *
 *	WHY THIS IS A HEADER AND NOT PART OF mm_pause.  mm_pause is
 *	compiled into bcrun; the poll and the pulse table are statics in
 *	headers compiled into the program.  bcrun's native table holds
 *	FUNCTIONS ONLY - there is no way for it to see a bytecode symbol,
 *	and no way for a native to call back into the VM if it could.  So
 *	the loop lives here, in generated code, and calls mm_pause one
 *	slice at a time.  A hook pointer in the runtime would have passed
 *	every gate on the host and failed on the board, which is the whole
 *	reason this note exists.
 *
 *	THE SLICE IS ASKED FOR, NOT ASSUMED.  Sleeping is what makes PAUSE
 *	a good citizen on a machine running several programs, and _pause()
 *	counts in DECISECONDS, so anything finer than 100 ms is a spin.
 *	Picking one number would be wrong both ways: a program with
 *	SETTICK 1000 would spin for nothing, and one with SETTICK 20 would
 *	have its tick quietly turned into a 100 ms one.  So each facility
 *	says how long it can be left alone - the shortest armed period,
 *	the time to go on a running pulse - and the wait takes the
 *	smallest.  A slow tick sleeps; a fast one spins, exactly as
 *	MMBasic does, and only while the program is actually pausing.
 *
 *	The deadline is taken ONCE, outside the loop, so time spent in a
 *	handler comes out of the pause rather than being added to it.
 *	That is MMBasic's behaviour too (its static PauseTimer survives an
 *	interrupted pause and the loop runs to the original end).
 */

#include "mmb_runtime.h"

#if defined(MM_PC3) || defined(__FUZIX__)
#define MMW_US()	pc3_us64()
#else
#define MMW_US()	((long long)mm_us())
#endif

/*	A decisecond: the longest sleep that costs nothing and the
 *	shortest that mm_pause can actually sleep rather than spin. */
#define MMW_SLICE_US	100000L

/*
 *	TWO MEASURES OF THE SAME WAIT, and the smaller wins.
 *
 *	`end` is a deadline off the clock, which is what makes time spent
 *	in a handler come out of the pause rather than being added to it.
 *	`togo` is the plain sum of the slices still owed.
 *
 *	The deadline alone is not safe everywhere.  On the board and under
 *	fcc the clock is TIMER0 and counts real microseconds, so it is
 *	exactly right.  In a plain host build mm_us falls back to clock(),
 *	which counts PROCESSOR time - it advanced 30 us across a 300 ms
 *	sleep when this was measured - so a deadline-only loop sleeps,
 *	learns that no time has passed, and sleeps again for ever.  That
 *	is not a hypothetical: it is what the first version of this
 *	function did, and it hung the gates.
 *
 *	Counting the slices down as well cannot stall, whatever the clock
 *	is doing, and costs nothing where the clock is real - there `end`
 *	is always the tighter of the two.
 */
MMG_FN void mm_wait(MMFLOAT ms)
{
	long long end, togo, left, slice, t;

	if (ms < 0)
		return;
	togo = (long long)(ms * 1000.0);
	end = MMW_US() + togo;
	for (;;) {
		/*	Service first, so a handler due at the start of the
		 *	wait runs at the start of it. */
#ifdef MMB_PULSE_H
		mmg_pulse_service();
#endif
#ifdef MMB_INT_H
		if (__mm_int_armed)
			mm_int_poll();
#endif
		left = end - MMW_US();
		if (left > togo)
			left = togo;
		if (left <= 0)
			return;

		slice = MMW_SLICE_US;
#ifdef MMB_INT_H
		t = mm_int_slice_us();
		if (t > 0 && t < slice)
			slice = t;
#endif
#ifdef MMB_PULSE_H
		t = mmg_pulse_slice_us();
		if (t > 0 && t < slice)
			slice = t;
#endif
		if (slice > left)
			slice = left;
		mm_pause((MMFLOAT)slice / 1000.0);
		togo -= slice;
	}
}

#endif /* MMB_WAIT_H */

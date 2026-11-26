#ifndef MMB_THREAD_H
#define MMB_THREAD_H
/*
 *	THREAD, the program's half.  The runtime's is mmb_thread.c, and
 *	PLAN-threads.md the design.
 *
 *	Here because what it needs is the program's own: the DATA pointer
 *	and GOSUB's returns, which each thread has its own of (mmb_data.h,
 *	mmb_misc.h), FLAG()'s word, which THREAD WAIT FLAG watches, and the
 *	interrupt poll, which a wait must go on taking (mmb_int.h) - the
 *	reason mmb_wait.h is a header too.  Included after all of them,
 *	and finds them by their include guards.
 *
 *	The translator writes MM_THREADS ahead of every include, so that
 *	mmb_int.h's poll asks the runtime for a switch every MMTH_EVERY
 *	statements and tells it when an interrupt routine runs.
 */

#include "mmb_runtime.h"
#include "mmb_int.h"            /* already in, in a translated program */
#include "mmb_wait.h"

/*	First thing in main: what each thread has a copy of.  A new thread
 *	starts with the values bound here - DATA read from the start, no
 *	GOSUB outstanding. */
MMG_FN void mmth_init(void)
{
	mm_thr_idword(&mmth_me);
#ifdef MMB_DATA_H
	mm_thr_bind(&mm_dptr, (MMINTEGER)sizeof mm_dptr);
	mm_thr_bind(mm_dstack, (MMINTEGER)sizeof mm_dstack);
	mm_thr_bind(&mm_dsp, (MMINTEGER)sizeof mm_dsp);
#endif
#ifdef MMB_MISC_H
	mm_thr_bind(mm_gstack, (MMINTEGER)sizeof mm_gstack);
	mm_thr_bind(&mm_gsp, (MMINTEGER)sizeof mm_gsp);
	mm_thr_flagword(&mm_flags_v);
#endif
}

/*	THREAD START's last step.  The first one arms the poll, which is
 *	where the threads take turns. */
static int mmth_armed;

MMG_FN void mmth_go(void (*entry)(void))
{
	mm_thr_go(entry);
	if (!mmth_armed) {
		mmth_armed = 1;
		mm_int_arm();
	}
}

/*
 *	THREAD LOCK, THREAD WAIT and THREAD WAIT FLAG: mm_thr_block a slice
 *	at a time, with this thread's interrupts taken between, exactly as
 *	mm_wait takes them through a PAUSE - so a thread that sets up a
 *	SETTICK and waits for the flag its routine sets is woken by it.
 *	`has` says whether a time limit was given (ms, which may not be
 *	negative).  1 when what it waited for came, 0 at the time limit,
 *	-1 at an error the runtime has raised.
 */
MMG_FN int mmth_wait(int kind, MMINTEGER arg, int has, MMFLOAT ms)
{
	long long end = -1, now, us, t;
	MMINTEGER r;
	unsigned n;

	if (has) {
		if (ms < 0) {
			mm_error("Number out of bounds");
			return -1;
		}
		end = MMW_US() + (long long)(ms * 1000.0);
	}
	for (;;) {
		now = MMW_US();
		us = MMW_SLICE_US;
		t = mm_int_slice_us();
		if (t > 0 && t < us)
			us = t;
		if (end >= 0 && end - now < us)
			us = end - now > 0 ? end - now : 0;
		r = mm_thr_block(kind, arg, us);
		if (r != 0)
			return r > 0 ? 1 : -1;
		if (end >= 0 && MMW_US() >= end)
			return 0;
		if (__mm_int_armed)
			do {
				n = mm_int_fired;
				mm_int_poll();
			} while (mm_int_fired != n);
	}
}

MMG_FN void mmth_lock(MMINTEGER k, int has, MMFLOAT ms)
{
	if (mmth_wait(MM_THR_LOCK, k, has, ms) == 0)
		mm_thr_lock_timeout(k);
}

#endif /* MMB_THREAD_H */

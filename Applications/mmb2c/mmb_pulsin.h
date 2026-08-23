#ifndef MMB_PULSIN_H
#define MMB_PULSIN_H
/*
 *	Pulsin( and Distance( - measuring a pulse on a machine that
 *	preempts.  PLAN-pulsin.md is the design; this is MMBasic's own
 *	state machine (misc/External.c:2548 and :2303) driven by the
 *	kernel's edge timestamps instead of by reading the pin.
 *
 *	WHY NOT A BUSY-WAIT, which is what the reference does.  Measured
 *	on the board with utils/spingap.c: a userland loop here is
 *	interrupted for 14-18us about 345 times a second by the system
 *	tick alone, and the moment a SECOND PROCESS is runnable the
 *	timeslice takes it off the CPU for HALF A SECOND.  A pulse
 *	measured across that is not noisy, it is nonsense - and nothing
 *	in a BASIC program could tell.  The kernel timestamps both edges
 *	in the GPIO interrupt (countpin.c, priority 0, RAM-resident), so
 *	this loop may be arbitrarily late and still return an exact
 *	width.  The remaining error is the interrupt latency: under a
 *	microsecond normally, up to ~18us if the tick happens to be
 *	running, whatever else the machine is doing.
 *
 *	Because of that, an edge is accepted or rejected BY ITS
 *	TIMESTAMP, not by when this loop noticed it: a poll that arrives
 *	late still returns exactly what MMBasic would have returned,
 *	including the -1 when the edge came after the timeout.
 *
 *	GP4-GP7 ONLY - the pins whose interrupt the kernel already owns
 *	for the counting inputs.  Any other pin is refused by name, which
 *	is the same bargain SETPIN FIN/CIN/PER made.  A number that is
 *	right when the machine is idle and wrong when it is busy would be
 *	worse than an honest refusal.
 */

#define MMG_CAP_FIRST	4
#define MMG_CAP_LAST	7

/*	Phase statuses from the core below. */
#define MMG_CAP_OK	0
#define MMG_CAP_T0	1		/* never left the active level */
#define MMG_CAP_T1	2		/* the pulse never started */
#define MMG_CAP_T2	3		/* it started and never ended */
#define MMG_CAP_LOST	4		/* the ring overflowed: cannot vouch */

/*	MMBasic's three spins, over timestamps.
 *
 *	  phase 0   the level is already `want`: wait for it to leave
 *	  phase 1   wait for the edge INTO `want` - the pulse starts
 *	  phase 2   wait for the edge out of it - the pulse ends
 *
 *	t_p0 and t_p1 bound phases 0 and 1 from the moment of arming, and
 *	they are separate because Distance( uses two different limits
 *	there where Pulsin( uses one for both (the reference does not
 *	re-zero its clock between them, and neither does this).  t_p2
 *	bounds the measurement from the pulse's own leading edge.
 */
static int mmg_cap_measure(MMINTEGER pin, int want,
			   unsigned long t_p0, unsigned long t_p1,
			   unsigned long t_p2, unsigned long *width)
{
	unsigned long t0, start = 0, seen = 0, quiet = 0;
	int phase, lvl, rc = MMG_CAP_LOST;
	MMINTEGER seq;

	if (mm_pincap(MM_PINCAP_ARM, pin, 1) < 0)
		return MMG_CAP_LOST;
	/*	The level at the moment of arming decides whether there is
	 *	a phase 0 at all - after this, levels come from the edges. */
	lvl = (int)pc3_pin_get((int)pin);
	t0 = (unsigned long)mm_us();
	phase = (lvl == want) ? 0 : 1;

	for (;;) {
		unsigned long now;

		seq = mm_pincap(MM_PINCAP_POLL, pin, 0);
		if (seq < 0)
			break;
		if ((unsigned long)seq - seen > MM_CAP_RING) {
			/*	More edges than the ring holds arrived
			 *	between two polls, so the sequence of levels
			 *	is broken.  MMBasic would have measured
			 *	SOMETHING here; we would rather say we could
			 *	not (the caller turns this into its own -1). */
			rc = MMG_CAP_LOST;
			break;
		}
		quiet = (seen == (unsigned long)seq) ? quiet + 1 : 0;
		while (seen < (unsigned long)seq) {
			unsigned long us =
			    (unsigned long)mm_pincap(MM_PINCAP_US, pin,
						     (MMINTEGER)(seen
							& (MM_CAP_RING - 1)));
			int el = (int)mm_pincap(MM_PINCAP_LVL, pin,
						(MMINTEGER)(seen
						    & (MM_CAP_RING - 1)));
			seen++;
			if (phase == 0) {
				if (el == want)
					continue;	/* not our edge yet */
				if (us - t0 > t_p0)
					return (mm_pincap(MM_PINCAP_OFF, pin,
							  0), MMG_CAP_T0);
				phase = 1;
			} else if (phase == 1) {
				if (el != want)
					continue;
				if (us - t0 > t_p1)
					return (mm_pincap(MM_PINCAP_OFF, pin,
							  0), MMG_CAP_T1);
				start = us;
				phase = 2;
			} else {
				if (el == want)
					continue;
				if (us - start > t_p2)
					return (mm_pincap(MM_PINCAP_OFF, pin,
							  0), MMG_CAP_T2);
				*width = us - start;
				return (mm_pincap(MM_PINCAP_OFF, pin, 0),
					MMG_CAP_OK);
			}
		}
		now = (unsigned long)mm_us();
		if (phase == 0 && now - t0 > t_p0) {
			rc = MMG_CAP_T0;
			break;
		}
		if (phase == 1 && now - t0 > t_p1) {
			rc = MMG_CAP_T1;
			break;
		}
		if (phase == 2 && now - start > t_p2) {
			rc = MMG_CAP_T2;
			break;
		}
		/*	Pace the polls: an ioctl is ~1.5us and there is
		 *	nothing to gain from asking faster than an edge can
		 *	arrive.  And on a line that has gone QUIET with a
		 *	long way still to wait, give the machine back -
		 *	sleeping here is safe only because the edge times
		 *	are the kernel's, so being late costs nothing.
		 *
		 *	Only when quiet, and that matters: the ring holds
		 *	sixteen edges, so sleeping through a busy line would
		 *	overflow it and turn a good measurement into -1. */
		if (quiet > 8 && phase != 2) {
			unsigned long left = (phase == 0 ? t_p0 : t_p1)
					     - (now - t0);
			if (left > 200000UL)
				mm_pause(100.0);
		}
		while ((unsigned long)mm_us() - now < 100UL)
			;
	}
	mm_pincap(MM_PINCAP_OFF, pin, 0);
	return rc;
}

/*	Pulsin(pin, polarity [, t1 [, t2]]) - microseconds, or -1.
 *
 *	The reference's own ranges: t1 and t2 default to 100000us and are
 *	5..10000000 (External.c:2562-2566), t2 defaults to t1, and the
 *	pin must already be a digital input.  Every timeout is -1, as it
 *	is there; the ring-overflow case is -1 too, because "I could not
 *	measure it" is what that means.
 */
MMG_FN MMINTEGER mmg_pulsin(MMINTEGER pin, MMINTEGER polarity,
			    MMINTEGER t1, MMINTEGER t2)
{
	unsigned long width = 0;
	int rc;

	/*	A negative t2 is the translator saying "not given": t2
	 *	defaults to t1, and passing the marker rather than the
	 *	expression twice keeps a t1 with a function call in it from
	 *	being evaluated twice, which MMBasic would not do. */
	if (t2 < 0)
		t2 = t1;
	if (pin < MMG_CAP_FIRST || pin > MMG_CAP_LAST)
		MM_RAISEV("Pulsin needs GP4, GP5, GP6 or GP7", -1);
	if (mmg_mode[pin] != MMG_PIN_DIN && mmg_mode[pin] != MMG_PIN_INTH
	    && mmg_mode[pin] != MMG_PIN_INTL
	    && mmg_mode[pin] != MMG_PIN_INTB)
		MM_RAISEV("Pin is not an input", -1);
	if (t1 < 5 || t1 > 10000000 || t2 < 5 || t2 > 10000000)
		MM_RAISEV("Number out of bounds", -1);
	rc = mmg_cap_measure(pin, polarity ? 1 : 0, (unsigned long)t1,
			     (unsigned long)t1, (unsigned long)t2, &width);
	if (rc != MMG_CAP_OK)
		return -1;
	return (MMINTEGER)width;
}

/*	Distance(trig [, echo]) - centimetres, -1 no echo, -2 no
 *	acknowledgement (External.c:2303-2352).
 *
 *	The trigger is generated exactly as the reference generates it:
 *	low, output, high for 20us, low, settle 50us, and for a 3-pin
 *	device the same wire goes back to being an input with its pull-up
 *	before the 50us that follows.  Those three delays are SPINS, as
 *	MMBasic's uSec() is - they make a pulse rather than measure one,
 *	and a sensor that wants at least 10us does not mind a longer one.
 *
 *	The measurement itself is the capture path, with the reference's
 *	own three limits: 50ms to see the line go idle, 100ms for the
 *	echo to start, 38ms for it to end.  58us per centimetre.
 */
MMG_FN MMFLOAT mmg_distance(MMINTEGER trig, MMINTEGER echo)
{
	unsigned long width = 0, t;
	int rc;

	if (echo < 0)			/* not given: the 3-pin device */
		echo = trig;
	if (echo < MMG_CAP_FIRST || echo > MMG_CAP_LAST)
		MM_RAISEV("Distance needs GP4, GP5, GP6 or GP7 for the echo",
			  (MMFLOAT)-1.0);
	if (trig < 0 || trig >= MM_GPIO_NPINS)
		MM_RAISEV("Invalid pin", (MMFLOAT)-1.0);
	if (mmg_claim(trig, MM_PLK_PIN) || mmg_claim(echo, MM_PLK_PIN))
		MM_RAISEV("Pin cannot do that", (MMFLOAT)-1.0);

	pc3_pin_in((int)echo, 1);		/* echo in, pull-up */
	pc3_pin_put((int)trig, 0);
	pc3_pin_out((int)trig);
	pc3_pin_put((int)trig, 1);
	t = (unsigned long)mm_us();
	while ((unsigned long)mm_us() - t < 20UL)
		;
	pc3_pin_put((int)trig, 0);
	t = (unsigned long)mm_us();
	while ((unsigned long)mm_us() - t < 50UL)
		;
	pc3_pin_in((int)echo, 1);		/* 3-pin device: same wire */
	t = (unsigned long)mm_us();
	while ((unsigned long)mm_us() - t < 50UL)
		;

	rc = mmg_cap_measure(echo, 1, 50000UL, 100000UL, 38000UL, &width);
	if (rc == MMG_CAP_T0 || rc == MMG_CAP_T1 || rc == MMG_CAP_LOST)
		return (MMFLOAT)-2.0;
	if (rc != MMG_CAP_OK)
		return (MMFLOAT)-1.0;
	return (MMFLOAT)width / 58.0;
}

#endif /* MMB_PULSIN_H */

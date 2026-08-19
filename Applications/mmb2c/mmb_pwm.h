#ifndef MMB_PWM_H
#define MMB_PWM_H
/*
 *	PWM, and the arithmetic is MMBasic's - not similar to it.
 *
 *	    PWM slice, frequency, duty1 [, duty2]
 *	    PWM slice, OFF
 *
 *	MMBasic's cmd_pwm (External.c:3093) works out the wrap and the
 *	channel levels in that order and no other, and the order matters
 *	because each step feeds the next:
 *
 *	    wrap  = cpu_hz / frequency
 *	    high  = cpu_khz / frequency * duty * 10
 *	    while wrap > 65535: halve wrap and both highs, double div
 *	    if div > 256: "Invalid frequency"
 *	    wrap--, and each non-zero high--
 *
 *	The halving loop is what makes a low frequency reachable at all -
 *	the counter is sixteen bits, so 375MHz over anything below about
 *	5.7kHz will not fit and the clock divider has to take up the
 *	slack.  Doing the duty arithmetic BEFORE the halving, and halving
 *	the answers with it, is what keeps the ratio exact rather than
 *	recomputing it against a divided clock and collecting a rounding
 *	error per step.
 *
 *	A NEGATIVE duty means inverted output - MMBasic turns the sign
 *	into the channel's polarity bit and uses the magnitude.
 *
 *	Slice, not pin: one slice drives two pins, and on the RP2350B
 *	twelve slices cover forty-eight, so pins alias (GP34 and GP42 are
 *	the same channel).  SETPIN pin, PWM claims the slice as well as
 *	the pin, which is what stops two programs sharing one by accident.
 */

#include "mmb_runtime.h"
#include "mmb_gpio.h"

/*	The PC3 runs at 375MHz.  MMBasic reads Option.CPU_Speed, which is
 *	the same number in kHz; there is no OPTION CPU here, so it is a
 *	constant and the one place to change if the clock ever moves. */
#define MMP_CPU_KHZ	375000L

#define MMP_NSLICE	12

MMG_FN void mmp_pwm_off(MMINTEGER slice)
{
	if (slice < 0 || slice >= MMP_NSLICE) {
		mm_error("Invalid PWM channel");
		return;
	}
	pc3_pwm_enable((int)slice, 0);
}

/*	have2 says whether the caller gave a second duty at all.  It is a
 *	flag rather than a sentinel because MMBasic's own "-1 means leave
 *	it alone" collides with a negative duty meaning INVERTED, and
 *	because a servo position of -20 is legal - there is no spare
 *	value to steal.  Omitting it leaves channel B exactly as it was,
 *	which is MMBasic's behaviour and matters: two servos share a
 *	slice, and setting one must not stop the other. */
MMG_FN void mmp_pwm2(MMINTEGER slice, MMFLOAT freq, MMFLOAT duty1,
		     MMFLOAT duty2, MMINTEGER have2)
{
	long div = 1, wrap, high1 = 0, high2 = 0;
	int inv1 = 0, inv2 = 0;

	if (slice < 0 || slice >= MMP_NSLICE) {
		mm_error("Invalid PWM channel");
		return;
	}
	if (freq <= 0 || freq > (MMFLOAT)(MMP_CPU_KHZ / 4) * 1000.0) {
		mm_error("Invalid frequency");
		return;
	}
	/*	A negative duty is MMBasic's way of asking for an inverted
	 *	output; the magnitude is the duty. */
	if (duty1 < 0) { duty1 = -duty1; inv1 = 1; }
	if (have2 && duty2 < 0) { duty2 = -duty2; inv2 = 1; }
	if (duty1 > 100.0 || (have2 && duty2 > 100.0)) {
		mm_error("Invalid duty cycle");
		return;
	}

	wrap = (long)((MMFLOAT)MMP_CPU_KHZ * 1000.0 / freq);
	high1 = (long)((MMFLOAT)MMP_CPU_KHZ / freq * duty1 * 10.0);
	if (have2)
		high2 = (long)((MMFLOAT)MMP_CPU_KHZ / freq * duty2 * 10.0);
	while (wrap > 65535) {
		wrap >>= 1;
		high1 >>= 1;
		high2 >>= 1;
		div <<= 1;
	}
	if (div > 256) {
		mm_error("Invalid frequency");
		return;
	}
	wrap--;
	if (high1)
		high1--;
	if (high2)
		high2--;

	/*	Configure stopped, set both levels, then start - so the
	 *	slice never runs for an instant with the new wrap and the
	 *	old duty, which on a motor or a servo is a real glitch and
	 *	not a theoretical one. */
	pc3_pwm_config((int)slice, (unsigned long)div, (unsigned long)wrap,
		       inv1, inv2, 0);
	pc3_pwm_level((int)slice, 0, (unsigned long)high1);
	if (have2)
		pc3_pwm_level((int)slice, 1, (unsigned long)high2);
	pc3_pwm_enable((int)slice, 1);
}

/*
 *	SERVO slice, position1 [, position2]
 *	SERVO slice, OFF
 *
 *	A servo is PWM at a fixed 50Hz frame with the position expressed
 *	as a pulse width, and MMBasic's mapping is one line (cmd_Servo,
 *	External.c:3013):
 *
 *	    duty = 5.0 + position * 0.05
 *
 *	so 0 is a 1.0ms pulse, 50 is 1.5ms (centre) and 100 is 2.0ms, in
 *	a 20ms frame.  The range is -20 to 120, which is the over-travel
 *	most servos will accept: 0.8ms to 2.2ms.
 *
 *	Everything after that is the PWM path, so it is the PWM path -
 *	the same arithmetic, the same divider, and the same "leave
 *	channel B alone if you did not mention it", which is what lets
 *	two servos share a slice.
 */
MMG_FN void mms_servo(MMINTEGER slice, MMFLOAT p1, MMFLOAT p2,
		      MMINTEGER have2)
{
	if (p1 > 120.0 || p1 < -20.0 || (have2 && (p2 > 120.0 || p2 < -20.0))) {
		mm_error("Invalid servo position");
		return;
	}
	mmp_pwm2(slice, 50.0, 5.0 + p1 * 0.05, 5.0 + p2 * 0.05, have2);
}

#endif /* MMB_PWM_H */

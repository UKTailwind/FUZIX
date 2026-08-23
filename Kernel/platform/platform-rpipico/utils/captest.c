/* captest - the edge capture behind Pulsin( and Distance(.
 *
 *   captest [pulses] [high_us]
 *
 * Drives GP3 with a PWM pulse of a known width and measures it through
 * the kernel's capture ring on GP5 (the bench link).  The PWM and the
 * timer the interrupt stamps with are both crystal-derived, so a
 * correct reading is EXACT: any spread is interrupt latency and
 * nothing else.
 *
 * The test that matters is the second one:
 *
 *   ./captest                 alone
 *   ./spingap 9 & ./captest   with the machine's CPU given away
 *
 * A busy-wait measurement collapses under the second (utils/spingap.c
 * measured the timeslice at half a second); a timestamped one should
 * not move at all.  That difference is the whole reason the capture
 * exists - PLAN-pulsin.md.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <sys/pc3io.h>
#include "../pico_ioctl.h"

#define SIG_PIN		3		/* PWM out */
#define CAP_PIN		5		/* capture in, wired to SIG_PIN */

/*	The PWM counter's rate with this divider, and it is NOT the SDK
 *	default's: this machine's clk_sys is 375MHz, so 375e6/250 gives
 *	1.5 counts per microsecond.  cnttest.c uses the same 1500000 for
 *	the same reason.  Getting this wrong is silent - the measurement
 *	comes back exact and disagrees with what you asked for, which is
 *	how the first run of this test read 100us for a 250us request. */
#define PWM_DIV		250
#define PWM_PER_US	3		/* counts per microsecond, times 2 */

static int gfd;

static void pwm_pulse(long high_us, long period_us)
{
	int slice = pc3_pwm_slice(SIG_PIN);
	int chan = pc3_pwm_chan(SIG_PIN);
	long top = (period_us * PWM_PER_US) / 2 - 1;
	long lvl = (high_us * PWM_PER_US) / 2;

	pc3_pwm_enable(slice, 0);
	pc3_pwm_config(slice, PWM_DIV, top, 0, 0, 0);
	pc3_pwm_level(slice, chan, lvl);
	pc3_pwm_pin(SIG_PIN);
	pc3_pwm_enable(slice, 1);
}

int main(int argc, char *argv[])
{
	struct capreq cq;
	int want = 20;
	long high_us = 250, period_us = 1000;
	unsigned long seen = 0, n = 0;
	long lo = 0x7fffffffL, hi = 0, sum = 0;
	unsigned long start = 0;
	int have_start = 0;
	long long deadline;

	if (argc >= 2)
		want = atoi(argv[1]);
	if (argc >= 3)
		high_us = atol(argv[2]);

	gfd = open("/dev/gpio", O_RDWR);
	if (gfd < 0) {
		perror("/dev/gpio");
		return 1;
	}
	if (pc3_claim(PLK_PIN, SIG_PIN) || pc3_claim(PLK_PIN, CAP_PIN)
	    || pc3_claim(PLK_PWM, pc3_pwm_slice(SIG_PIN))) {
		perror("claim");
		return 1;
	}
	pc3_pin_in(CAP_PIN, 0);
	pwm_pulse(high_us, period_us);

	memset(&cq, 0, sizeof(cq));
	cq.pin = CAP_PIN;
	cq.arg = 1;
	if (ioctl(gfd, GPIOC_CNT_CAP, &cq)) {
		perror("arm");
		return 1;
	}
	printf("captest: GP%d -> GP%d, %ld us high in %ld us, %d pulses\n",
	       SIG_PIN, CAP_PIN, high_us, period_us, want);

	deadline = pc3_us64() + 5000000LL;
	while ((int)n < want && pc3_us64() < deadline) {
		unsigned long seq;
		unsigned i;

		cq.pin = CAP_PIN;
		if (ioctl(gfd, GPIOC_CNT_CAPRD, &cq)) {
			perror("read");
			return 1;
		}
		seq = cq.seq;
		if (seq - seen > PC3_CAP_RING) {
			printf("  ring overflowed (%lu behind), resyncing\n",
			       seq - seen - PC3_CAP_RING);
			seen = seq - PC3_CAP_RING;
			have_start = 0;
		}
		for (; seen < seq; seen++) {
			i = seen & (PC3_CAP_RING - 1);
			if (cq.lvl & (1UL << i)) {
				start = cq.us[i];
				have_start = 1;
			} else if (have_start) {
				long w = (long)(cq.us[i] - start);

				have_start = 0;
				if (w < lo)
					lo = w;
				if (w > hi)
					hi = w;
				sum += w;
				n++;
			}
		}
	}
	cq.pin = CAP_PIN;
	cq.arg = 0;
	ioctl(gfd, GPIOC_CNT_CAP, &cq);
	pc3_pwm_enable(pc3_pwm_slice(SIG_PIN), 0);

	if (n == 0) {
		printf("  NO PULSES - is GP%d linked to GP%d?\n",
		       SIG_PIN, CAP_PIN);
		return 1;
	}
	printf("  %lu pulses: min %ld us, max %ld us, mean %ld us"
	       " (want %ld)\n", n, lo, hi, sum / (long)n, high_us);
	return 0;
}

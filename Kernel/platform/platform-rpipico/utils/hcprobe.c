/* hcprobe - what does the HC-SR04 actually put on the echo pin?
 *
 *   hcprobe [trig] [echo] [pullup]
 *
 * Distance( read 0.1 cm - a ~6 us pulse - where a real echo is
 * hundreds of microseconds.  A number that plausible is worth
 * distrusting, so this prints the RAW edges instead of interpreting
 * them: capture is armed BEFORE the trigger, so the trigger's own
 * effect on the line is visible too, and every edge is reported with
 * its level and its time from the falling edge of the trigger.
 *
 * The default pull-up follows the reference (fun_distance sets CNPUSET
 * on the echo pin); pass 0 to leave the line floating and see whether
 * that is what is generating the pulse.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <sys/pc3io.h>
#include "../pico_ioctl.h"

int main(int argc, char *argv[])
{
	struct capreq cq;
	int trig = 1, echo = 7, pullup = 1;
	int gfd;
	unsigned long seen = 0, t0;
	long long deadline;

	if (argc >= 2)
		trig = atoi(argv[1]);
	if (argc >= 3)
		echo = atoi(argv[2]);
	if (argc >= 4)
		pullup = atoi(argv[3]);

	gfd = open("/dev/gpio", O_RDWR);
	if (gfd < 0) {
		perror("/dev/gpio");
		return 1;
	}
	if (pc3_claim(PLK_PIN, trig) || pc3_claim(PLK_PIN, echo)) {
		perror("claim");
		return 1;
	}

	pc3_pin_in(echo, pullup);
	printf("hcprobe: trig GP%d, echo GP%d, pull-up %d\n",
	       trig, echo, pullup);
	printf("  echo reads %d before the trigger\n", pc3_pin_get(echo));

	memset(&cq, 0, sizeof(cq));
	cq.pin = echo;
	cq.arg = 1;
	if (ioctl(gfd, GPIOC_CNT_CAP, &cq)) {
		perror("arm");
		return 1;
	}

	/* the reference's trigger: low, output, 20us high, low */
	pc3_pin_put(trig, 0);
	pc3_pin_out(trig);
	pc3_pin_put(trig, 1);
	t0 = (unsigned long)pc3_us64();
	while ((unsigned long)pc3_us64() - t0 < 20UL)
		;
	pc3_pin_put(trig, 0);
	t0 = (unsigned long)pc3_us64();

	/*	A second opinion, taken a completely different way: sample
	 *	the pin in a tight loop (~140ns per read) and report the
	 *	transitions.  If the interrupt capture and the CPU
	 *	disagree, the fault is ours; if they agree, it is the
	 *	wire's.  stripscope.c used the same trick on the WS2812
	 *	timing when there was no instrument to hand. */
	if (argc >= 5 && strcmp(argv[4], "-s") == 0) {
		/*	BOTH mechanisms on the SAME pulse, which is the only
		 *	way to tell a marginal wire from a bug: sample into
		 *	memory (printing would perturb the timing), then read
		 *	the kernel's ring afterwards and print the two
		 *	streams together. */
		long st[32];
		int sl[32];
		int ns = 0, last = pc3_pin_get(echo), now;
		unsigned long s0 = (unsigned long)pc3_us64();
		unsigned i;

		while ((unsigned long)pc3_us64() - s0 < 60000UL) {
			now = pc3_pin_get(echo);
			if (now != last) {
				if (ns < 32) {
					st[ns] = (long)((unsigned long)
							pc3_us64() - t0);
					sl[ns] = now;
					ns++;
				}
				last = now;
			}
		}
		cq.pin = echo;
		ioctl(gfd, GPIOC_CNT_CAPRD, &cq);
		printf("  CPU sampled %d transitions:\n", ns);
		for (i = 0; i < (unsigned)ns; i++)
			printf("    %6ld us  -> %d\n", st[i], sl[i]);
		printf("  the interrupt recorded %lu:\n", cq.seq);
		for (i = 0; i < cq.seq && i < PC3_CAP_RING; i++)
			printf("    %6ld us  -> %d  (events %lu)\n",
			       (long)(cq.us[i] - t0),
			       (cq.lvl & (1UL << i)) ? 1 : 0, cq.ev[i]);
		cq.pin = echo;
		cq.arg = 0;
		ioctl(gfd, GPIOC_CNT_CAP, &cq);
		return 0;
	}

	printf("  edges, us after the trigger:\n");
	deadline = pc3_us64() + 300000LL;	/* 300 ms of looking */
	while (pc3_us64() < deadline) {
		unsigned long seq;

		cq.pin = echo;
		if (ioctl(gfd, GPIOC_CNT_CAPRD, &cq)) {
			perror("read");
			break;
		}
		seq = cq.seq;
		if (seq - seen > PC3_CAP_RING) {
			printf("    (ring overflowed, %lu edges lost)\n",
			       seq - seen - PC3_CAP_RING);
			seen = seq - PC3_CAP_RING;
		}
		for (; seen < seq; seen++) {
			unsigned i = seen & (PC3_CAP_RING - 1);

			printf("    %6ld us  -> %d  (events %lu)\n",
			       (long)(cq.us[i] - t0),
			       (cq.lvl & (1UL << i)) ? 1 : 0, cq.ev[i]);
		}
	}
	cq.pin = echo;
	cq.arg = 0;
	ioctl(gfd, GPIOC_CNT_CAP, &cq);
	printf("  %lu edges in 300 ms; echo now reads %d\n", seen,
	       pc3_pin_get(echo));
	return 0;
}

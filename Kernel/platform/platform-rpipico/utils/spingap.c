/* spingap - how long is a userland timing loop interrupted for?
 *
 *   spingap [seconds] [-v]
 *
 * The question behind Pulsin( and Distance(.  MMBasic measures a pulse
 * by spinning on a pin and a microsecond clock, and the 2026-08-22
 * review cleared the family on the grounds that the reference never
 * disables interrupts.  That was the wrong question for THIS machine:
 * what corrupts a microsecond MEASUREMENT here is not a mask, it is
 * being taken off the CPU in the middle of one.  Two things do that
 * without any second process needing to exist:
 *
 *   - the 5 ms system tick, which holds di() across its whole body
 *     (devices.c: usbkbd_tick, tty_interrupt, rawuart_tx_poll,
 *     timer_interrupt, sound_pcm_tick);
 *   - the USB host pump, which devices.c injects into ANY userland
 *     spin longer than 5 ms by pending PendSV (usbkbd_starved), so a
 *     long measurement attracts it by definition.
 *
 * So this reads the microsecond clock in the tightest loop there is
 * and reports the GAPS: how big, how often, and how much of the wall
 * clock they took.  That number is what decides whether Pulsin can be
 * an honest userland busy-wait here, and with what error, or whether
 * it needs the hardware.
 *
 * Run it three ways, because the answer is not one number:
 *
 *   ./spingap 5                 alone, the floor
 *   ./spingap 5 & ./spingap 5   two runnable processes: preemption too
 *   (with audio playing, and with a key held down)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/pc3io.h>

/* Gap buckets, in microseconds.  10 is well above the loop's own
 * period, so anything counted here is an interruption and not us. */
static const long bucket[] = { 10, 25, 50, 100, 250, 500, 1000, 5000,
			       20000, 100000 };
#define NBUCKET (int)(sizeof(bucket) / sizeof(bucket[0]))

#define NBIG 12				/* biggest gaps, kept in full */

int main(int argc, char *argv[])
{
	long long t0, now, prev, gap, worst = 0, lost = 0;
	long long big[NBIG], bigat[NBIG];
	unsigned long count[NBUCKET];
	unsigned long long iters = 0;
	int seconds = 5, verbose = 0, i, k, nbig = 0;

	if (argc >= 2)
		seconds = atoi(argv[1]);
	if (argc >= 3 && strcmp(argv[2], "-v") == 0)
		verbose = 1;
	if (seconds < 1)
		seconds = 1;
	memset(count, 0, sizeof(count));
	memset(big, 0, sizeof(big));
	memset(bigat, 0, sizeof(bigat));

	t0 = prev = pc3_us64();
	for (;;) {
		now = pc3_us64();
		gap = now - prev;
		prev = now;
		iters++;
		if (gap >= bucket[0]) {
			lost += gap;
			for (i = NBUCKET - 1; i >= 0; i--)
				if (gap >= bucket[i]) {
					count[i]++;
					break;
				}
			if (gap > worst)
				worst = gap;
			/* keep the biggest few, with when they happened -
			 * a periodic gap and a one-off look identical in
			 * a histogram and quite different in a list */
			if (nbig < NBIG) {
				big[nbig] = gap;
				bigat[nbig] = now - t0;
				nbig++;
			} else {
				k = 0;
				for (i = 1; i < NBIG; i++)
					if (big[i] < big[k])
						k = i;
				if (gap > big[k]) {
					big[k] = gap;
					bigat[k] = now - t0;
				}
			}
		}
		if (now - t0 >= (long long)seconds * 1000000LL)
			break;
	}

	now = pc3_us64();
	printf("spingap: %d s, %llu samples, %lld us per sample\n",
	       seconds, iters, (now - t0) / (long long)iters);
	printf("  worst gap %lld us, %lld us lost in gaps (%lld ppm)\n",
	       worst, lost, lost * 1000000LL / (now - t0));
	for (i = 0; i < NBUCKET; i++) {
		if (count[i] == 0)
			continue;
		if (i == NBUCKET - 1)
			printf("  >= %6ld us : %lu\n", bucket[i], count[i]);
		else
			printf("  %6ld - %6ld us : %lu\n", bucket[i],
			       bucket[i + 1] - 1, count[i]);
	}
	if (verbose) {
		printf("  biggest, at ms into the run:\n");
		for (i = 0; i < nbig; i++)
			printf("    %6lld us at %lld ms\n", big[i],
			       bigat[i] / 1000);
	}
	return 0;
}

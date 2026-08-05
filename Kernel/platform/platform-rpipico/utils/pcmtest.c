/*
 * pcmtest - prove the kernel's PCM sink with no decoder anywhere near it.
 *
 *   pcmtest [seconds] [-m] [-r rate] [-s]
 *
 *	-m	mono, to exercise the driver's duplicate-to-both-channels
 *		path rather than the straight copy
 *	-r	sample rate (default 44100)
 *	-s	starve deliberately: stop feeding half way, so the
 *		underrun counter can be seen to work.  A counter that has
 *		never been non-zero is not evidence of anything.
 *
 * Plays a 440 Hz tone through SNDIOC_PCMOPEN/WRITE/STAT/CLOSE and
 * reports the underrun count.  The point is to separate two failures
 * that would otherwise arrive together: a broken ring, and a decoder
 * too slow to fill it.  If this is clean, anything wrong with playmp3
 * is playmp3's.
 *
 * No floating point: the tone comes from a 64-entry table and a
 * fixed-point phase accumulator.  Nothing here needs the FPU, and
 * keeping it out means this program can be built and run on a kernel
 * without CP10/CP11 granted.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>

#include "../pico_ioctl.h"

/* One cycle of a sine, +/-8000 - a quarter of full scale, which is
 * loud enough to hear a glitch and quiet enough not to hurt. */
static const short sine[64] = {
	     0,    784,   1561,   2322,   3061,   3771,   4445,   5075,
	  5657,   6184,   6652,   7055,   7391,   7656,   7846,   7961,
	  8000,   7961,   7846,   7656,   7391,   7055,   6652,   6184,
	  5657,   5075,   4445,   3771,   3061,   2322,   1561,    784,
	     0,   -784,  -1561,  -2322,  -3061,  -3771,  -4445,  -5075,
	 -5657,  -6184,  -6652,  -7055,  -7391,  -7656,  -7846,  -7961,
	 -8000,  -7961,  -7846,  -7656,  -7391,  -7055,  -6652,  -6184,
	 -5657,  -5075,  -4445,  -3771,  -3061,  -2322,  -1561,   -784
};

#define TONE_HZ  440
#define CHUNK    2048			/* frames generated per pass */

static short buf[CHUNK * 2];		/* worst case: stereo */

int main(int argc, char *argv[])
{
	struct snd_pcm cfg;
	struct snd_buf sb;
	struct snd_stat st;
	int fd, a, n, seconds = 5, mono = 0, starve = 0;
	unsigned long rate = 44100, total, done = 0;
	unsigned long phase = 0, inc;		/* 16.16 into the table */

	for (a = 1; a < argc; a++) {
		if (!strcmp(argv[a], "-m"))
			mono = 1;
		else if (!strcmp(argv[a], "-s"))
			starve = 1;
		else if (!strcmp(argv[a], "-r") && a + 1 < argc)
			rate = strtoul(argv[++a], NULL, 10);
		else
			seconds = atoi(argv[a]);
	}
	if (seconds <= 0)
		seconds = 5;

	fd = open("/dev/sys", O_RDWR);
	if (fd < 0) {
		perror("/dev/sys");
		return 1;
	}

	cfg.rate = rate;
	cfg.channels = mono ? 1 : 2;
	cfg.bits = 16;
	if (ioctl(fd, SNDIOC_PCMOPEN, &cfg) < 0) {
		perror("SNDIOC_PCMOPEN");
		return 1;
	}
	printf("%lu Hz %s, %d seconds of %d Hz%s\n", rate,
	       mono ? "mono" : "stereo", seconds, TONE_HZ,
	       starve ? ", starving half way" : "");

	/* table entries per frame, 16.16 */
	inc = (unsigned long)(((unsigned long long)TONE_HZ * 64 << 16) / rate);
	total = rate * (unsigned long)seconds;

	while (done < total) {
		int frames = CHUNK;
		int i, bytes;

		if ((unsigned long)frames > total - done)
			frames = (int)(total - done);

		/* Stop feeding for the middle second, if asked, and let the
		 * ring run dry on purpose. */
		if (starve && done > total / 2 && done < total / 2 + rate) {
			sleep(1);
			done += rate;
			continue;
		}

		for (i = 0; i < frames; i++) {
			short v = sine[(phase >> 16) & 63];
			phase += inc;
			if (mono) {
				buf[i] = v;
			} else {
				buf[i * 2] = v;
				buf[i * 2 + 1] = v;
			}
		}
		bytes = frames * (mono ? 2 : 4);

		/* Hand it over as the ring has room.  A short write is the
		 * normal case once playback has caught up, not an error. */
		for (i = 0; i < bytes; ) {
			sb.base = (char *)buf + i;
			sb.len = (unsigned long)(bytes - i);
			n = ioctl(fd, SNDIOC_PCMWRITE, &sb);
			if (n < 0) {
				perror("SNDIOC_PCMWRITE");
				ioctl(fd, SNDIOC_PCMCLOSE, 0);
				return 1;
			}
			if (n == 0)
				usleep(20000);	/* ring full: let it drain */
			i += n;
		}
		done += frames;
	}

	/* Split the count at the end of feeding.  Underruns while data is
	 * still being written are the ones that matter; anything after
	 * that is the ring draining out while this loop polls, which is
	 * an artefact of the test and not of the transport. */
	if (ioctl(fd, SNDIOC_PCMSTAT, &st) == 0)
		printf("underruns while feeding: %lu (queued %lu)\n",
		       (unsigned long)st.underruns, (unsigned long)st.queued);

	/* Play the tail out before closing: CLOSE is immediate and drops
	 * whatever is still queued.
	 *
	 * BOUNDED, and not as a matter of taste.  The first version span
	 * here forever because the driver could not consume a final block
	 * shorter than one DMA half-buffer, and an unbounded wait on a
	 * kernel counter turns a driver bug into a wedged machine that
	 * needs Ctrl-C from another terminal.  Two seconds is far longer
	 * than the ~1.5s the ring can hold. */
	for (a = 0; a < 100; a++) {
		if (ioctl(fd, SNDIOC_PCMSTAT, &st) < 0) {
			perror("SNDIOC_PCMSTAT");
			break;
		}
		if (st.queued == 0)
			break;
		usleep(20000);
	}
	if (st.queued)
		printf("DRAIN STALLED with %lu bytes queued\n",
		       (unsigned long)st.queued);
	printf("underruns: %lu\n", (unsigned long)st.underruns);

	ioctl(fd, SNDIOC_PCMCLOSE, 0);
	close(fd);
	return 0;
}

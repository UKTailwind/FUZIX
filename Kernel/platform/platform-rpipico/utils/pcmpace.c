/* pcmpace - pcmtest's sine through a PARAMETERISED feed pattern.
 *
 *   pcmpace [seconds] [chunk_frames] [target_bytes] [sleep_us]
 *
 * The clean players and the pulsing one differ only in how they pace
 * the ring: pcmtest fills it, playmp3 writes deep, playmod writes 4K
 * chunks on a 20 ms cadence, playsnd writes 2K chunks to a 16K target
 * on a 10 ms cadence.  This is the same 440 Hz sine as pcmtest fed
 * with any pattern you name - the minimal reproducer, no daemon, no
 * FIFO, no BASIC anywhere.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "../pico_ioctl.h"
#include "pc3sys.h"

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

#define TONE_HZ 440
#define MAXCHUNK 2048

static short buf[MAXCHUNK * 2];

int main(int argc, char *argv[])
{
	struct snd_pcm cfg;
	struct snd_buf sb;
	struct snd_stat st;
	int fd, n, seconds = 5, chunk = 512;
	long target = 16384, slp = 10000;
	unsigned long phase = 0, inc, done = 0, total;

	if (argc >= 2)
		seconds = atoi(argv[1]);
	if (argc >= 3)
		chunk = atoi(argv[2]);
	if (argc >= 4)
		target = atol(argv[3]);
	if (argc >= 5)
		slp = atol(argv[4]);
	if (chunk < 1 || chunk > MAXCHUNK) {
		fprintf(stderr, "chunk 1..%d frames\n", MAXCHUNK);
		return 1;
	}

	fd = pc3_open_sys();
	if (fd < 0) {
		perror("/dev/sys");
		return 1;
	}
	cfg.rate = 44100;
	cfg.channels = 2;
	cfg.bits = 16;
	if (pc3_ioctl(fd, SNDIOC_PCMOPEN, &cfg) < 0) {
		perror("SNDIOC_PCMOPEN");
		return 1;
	}
	printf("%ds, %d-frame chunks, %ld target, %ldus sleep\n",
	       seconds, chunk, target, slp);

	inc = (unsigned long)(((unsigned long long)TONE_HZ * 64 << 16) /
			      44100);
	total = 44100UL * (unsigned long)seconds;

	while (done < total) {
		if (pc3_ioctl(fd, SNDIOC_PCMSTAT, &st) < 0)
			break;
		while ((long)st.queued < target && done < total) {
			int i, off = 0, w, bytes = chunk * 4;

			for (i = 0; i < chunk; i++) {
				short v = sine[(phase >> 16) & 63];

				phase += inc;
				buf[i * 2] = v;
				buf[i * 2 + 1] = v;
			}
			while (off < bytes) {
				sb.base = (char *)buf + off;
				sb.len = (unsigned long)(bytes - off);
				w = pc3_ioctl(fd, SNDIOC_PCMWRITE, &sb);
				if (w < 0) {
					perror("write");
					goto out;
				}
				if (w == 0)
					usleep(20000);
				off += w;
			}
			done += (unsigned long)chunk;
			if (pc3_ioctl(fd, SNDIOC_PCMSTAT, &st) < 0)
				goto out;
		}
		usleep((unsigned long)slp);
	}
out:
	if (pc3_ioctl(fd, SNDIOC_PCMSTAT, &st) == 0)
		printf("underruns while feeding: %lu (queued %lu)\n",
		       (unsigned long)st.underruns,
		       (unsigned long)st.queued);
	/* The drain curve is the consumption-rate measurement: queued
	 * bytes / drain time should be rate*4 = 176400 B/s.  Poll on a
	 * 20 ms cadence and print the trajectory. */
	for (n = 0; n < 120; n++) {
		if (pc3_ioctl(fd, SNDIOC_PCMSTAT, &st) < 0 || st.queued == 0)
			break;
		if ((n % 5) == 0)
			printf("drain %d: %lu\n", n, (unsigned long)st.queued);
		usleep(20000);
	}
	printf("drained after %d polls (~%d ms)\n", n, n * 20);
	printf("final underruns: %lu\n", (unsigned long)st.underruns);
	pc3_ioctl(fd, SNDIOC_PCMCLOSE, 0);
	return 0;
}

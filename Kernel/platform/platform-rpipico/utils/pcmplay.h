/*
 * pcmplay - the half of a file player that has nothing to do with the
 * codec: the volume law, the kernel PCM sink, the drain and the signal.
 *
 * playmp3 was here first and playwav and playflac are the same program
 * with a different decoder in the middle.  Written out three times that
 * would be three copies of a volume table and a write loop, and this
 * codebase has spent a day proving what happens to anything written
 * down more than once.  One copy, included by all three.
 *
 * Static functions in a header, the way the mmb_gfx_* primitives are
 * done, so each player carries only what it names.
 *
 * WHAT IS NOT HERE: the FPU.  playmp3 must be built with hardware float
 * (minimp3 is single-precision throughout and soft float decodes at
 * 0.59x real time), and it is one of exactly two programs in the tree
 * permitted the flags, because the kernel saves no FP context across a
 * context switch and the rule is that at most one process has live FP
 * state.  WAV and FLAC are integer codecs and are built soft-float, so
 * they do not join that argument.  If a future player needs the FPU,
 * read PC3-MP3-PLAN.md before adding the flags: the invariant is held
 * by the audio device lock, and only because a player holds the PCM
 * stream for its whole life.
 */
#ifndef PCMPLAY_H
#define PCMPLAY_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "../pico_ioctl.h"

/* ---- memory: the PROCESS HEAP, deliberately not the PSRAM arena --------
 *
 * Measured with the MP3 decoder: the same decode runs at 2.91x real time
 * out of a PSRAM arena and 5.57x out of process memory, because every
 * touch of hot decoder state was going over the QMI instead of hitting
 * SRAM.  It costs more than speed - flash and PSRAM share that
 * controller, so sustained arena traffic starves anything fetching from
 * flash, which is most of the kernel.
 *
 * Bulk data touched once belongs in the arena; a decoder's working set
 * does not.  All three players allocate from the process heap.
 *
 * onRealloc is NULL in each player's callbacks, which is what MMBasic
 * passes too: none of these three decoders needs it, and a realloc of a
 * decoder's working buffer is the last thing a 336K pool wants.
 */
static void *pcm_malloc(size_t n, void *u) { (void)u; return malloc(n); }
static void pcm_free(void *p, void *u) { (void)u; free(p); }

/* ---- the file, behind whatever callback shape a decoder wants -------- */

static int pcm_fd = -1;                 /* the audio file being played */

static size_t pcm_fd_read(void *buf, size_t n)
{
	unsigned char *out = buf;
	size_t total = 0;
	int got;

	while (total < n) {
		got = read(pcm_fd, out + total, (int)(n - total));
		if (got <= 0)
			break;
		total += (size_t)got;
	}
	return total;
}

/* origin is the decoder's own enum; the callers map it to SEEK_*. */
static int pcm_fd_seek(int offset, int whence)
{
	return lseek(pcm_fd, (off_t)offset, whence) == (off_t)-1 ? 0 : 1;
}

/* ---- volume ------------------------------------------------------------
 *
 * MicroPython's law, not MMBasic's.  pcaudio's _compute_gain maps 0-100
 * onto an 8.8 fixed point gain over a ~50dB log taper,
 * 256 * 10^((v-100)/40), and audio.scale then applies it as
 * (sample * gain) >> 8.  MMBasic's i2sconvert is
 * sample * mapping[vol] / 2000 - a multiply AND A DIVIDE for every
 * sample of every frame.  Same curve, a shift instead of a divide, and
 * this is the per-sample path so it is the one that matters.
 *
 * Tabulated rather than computed so nothing here needs pow() and the
 * players do not drag in libm for one value.
 */
static const short pcm_vol_gain[101] = {
	   0,    1,    1,    1,    1,    1,    1,    1,    1,    1,
	   1,    2,    2,    2,    2,    2,    2,    2,    2,    2,
	   3,    3,    3,    3,    3,    3,    4,    4,    4,    4,
	   5,    5,    5,    5,    6,    6,    6,    7,    7,    8,
	   8,    9,    9,   10,   10,   11,   11,   12,   13,   14,
	  14,   15,   16,   17,   18,   19,   20,   22,   23,   24,
	  26,   27,   29,   30,   32,   34,   36,   38,   41,   43,
	  46,   48,   51,   54,   57,   61,   64,   68,   72,   76,
	  81,   86,   91,   96,  102,  108,  114,  121,  128,  136,
	 144,  152,  162,  171,  181,  192,  203,  215,  228,  242,
	 256
};

static void pcm_apply_gain(short *s, int n, int gain)
{
	int i;

	if (gain >= 256)
		return;                 /* unity: nothing to do */
	if (gain <= 0) {
		memset(s, 0, (size_t)n * sizeof(short));
		return;
	}
	for (i = 0; i < n; i++)
		s[i] = (short)(((long)s[i] * gain) >> 8);
}

static int pcm_volume_arg(const char *s)
{
	int v = atoi(s);

	if (v < 0)
		v = 0;
	if (v > 100)
		v = 100;
	return v;
}

/* ---- the kernel sink ---------------------------------------------------
 *
 * 2048 frames is MMBasic's WAV_BUFFER_SIZE (8192 bytes stereo) - a
 * proven figure, and comfortably below the kernel ring's 256K.  MMBasic
 * uses the same buffer for all three formats, so this one does too.
 */
#define PCM_BLOCK 2048
static short pcm_buf[PCM_BLOCK * 2];

static volatile int pcm_stopping;

static void pcm_on_intr(int sig)
{
	(void)sig;
	pcm_stopping = 1;               /* close tidily rather than leaving
					   1.5 seconds of ring playing on */
}

/* Open the I2S sink.  Returns the /dev/sys fd, or -1 having said why.
 * `who` is the program name for the messages. */
static int pcm_open(const char *who, unsigned long rate, int chans)
{
	struct snd_pcm cfg;
	int sfd;

	if (chans < 1 || chans > 2) {
		fprintf(stderr, "%s: %d channels, this plays mono or stereo\n",
			who, chans);
		return -1;
	}
	sfd = open("/dev/sys", O_RDWR);
	if (sfd < 0) {
		perror("/dev/sys");
		return -1;
	}
	cfg.rate = rate;
	cfg.channels = (unsigned short)chans;
	cfg.bits = 16;
	if (ioctl(sfd, SNDIOC_PCMOPEN, &cfg) < 0) {
		/* There is one I2S engine and one owner of it.  Say which of
		 * the two things went wrong: "busy" sent people looking at
		 * the sample rate for a fault that was a second player. */
		if (errno == EBUSY)
			fprintf(stderr, "%s: sound output in use by pid %d\n",
				who, ioctl(sfd, SNDIOC_PCMOWNER, 0));
		else
			fprintf(stderr, "%s: cannot start audio at %lu Hz\n",
				who, rate);
		close(sfd);
		return -1;
	}
	signal(SIGINT, pcm_on_intr);
	return sfd;
}

/* Hand a decoded block over as the ring has room.  A short write is the
 * normal case once playback has caught up: the decoders run faster than
 * real time, so most of this is spent waiting rather than decoding.
 * Returns 0, or -1 on a write fault (having set pcm_stopping). */
static int pcm_write(int sfd, const short *s, int samples)
{
	struct snd_buf sb;
	int bytes = samples * (int)sizeof(short);
	int i, n;

	for (i = 0; i < bytes && !pcm_stopping; ) {
		sb.base = (char *)s + i;
		sb.len = (unsigned long)(bytes - i);
		n = ioctl(sfd, SNDIOC_PCMWRITE, &sb);
		if (n < 0) {
			perror("SNDIOC_PCMWRITE");
			pcm_stopping = 1;
			return -1;
		}
		if (n == 0)
			usleep(20000);
		i += n;
	}
	return 0;
}

/* Let the ring play out, unless we are stopping on a signal - then the
 * point is to stop now.  CLOSE drops whatever is still queued.  Bounded,
 * so a driver fault cannot wedge the shell. */
static void pcm_close(const char *who, int sfd)
{
	struct snd_stat st;
	int i;

	if (!pcm_stopping) {
		for (i = 0; i < 200; i++) {
			if (ioctl(sfd, SNDIOC_PCMSTAT, &st) < 0)
				break;
			if (st.queued == 0)
				break;
			usleep(20000);
		}
	}
	if (ioctl(sfd, SNDIOC_PCMSTAT, &st) == 0 && st.underruns)
		fprintf(stderr, "%s: %lu underruns\n", who,
			(unsigned long)st.underruns);
	ioctl(sfd, SNDIOC_PCMCLOSE, 0);
	close(sfd);
}

#endif /* PCMPLAY_H */

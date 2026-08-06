/*
 * playmp3 - play an MP3 file through the PCM5102.
 *
 *   playmp3 file.mp3 [volume]        volume 0-100, default 80
 *
 * MMBasic's PLAY MP3 as a program, on the model saveimage and loadimage
 * set: a whole operation is a program and not runtime, so a BASIC
 * program that never plays a note pays nothing for the decoder.
 *
 * Three pieces, each proven separately before this existed
 * (PC3-MP3-PLAN.md):
 *
 *   - dr_mp3, configured as MMBasic configures it, measured at 3x real
 *     time on this board with the FPU enabled and 0.6x without it;
 *   - the kernel's PCM sink, SNDIOC_PCMOPEN/WRITE/STAT/CLOSE, which
 *     feeds the same chained-DMA I2S engine the BBC synth uses;
 *   - MicroPython's volume law, below.
 *
 * Because this is a separate process, playback carries on while a BASIC
 * program runs: there is no idle-loop refill anywhere, which is what
 * MMBasic needs checkWAVinput() for. That is what the kernel's deep
 * ring is really buying.
 *
 * MUST be built with the FPU (see the Makefile). minimp3 is single
 * precision float throughout and soft float does not reach real time.
 * That also means this is one of exactly two programs allowed to carry
 * -mfpu, and it holds the PCM stream while it runs - the audio device
 * lock doubles as the FPU lock, because no FP context is saved across
 * a context switch.  The kernel enforces that lock: SNDIOC_PCMOPEN
 * fails with EBUSY while another process holds the stream, so two of
 * these cannot run at once.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "../pico_ioctl.h"

/* ---- memory: the PROCESS HEAP, deliberately not the PSRAM arena --------
 *
 * The plan had the decoder's ~65K (the drmp3 struct plus its 32K input
 * chunk) in a PSRAM arena, on the assumption that it would not fit a
 * process.  It fits, and the arena was costing about half the speed:
 * measured on the board, the same decode runs at 2.91x real time out of
 * the arena and 5.57x out of process memory, because every touch of 32K
 * of hot decoder state was going over the QMI instead of hitting SRAM.
 *
 * It costs more than speed.  Flash and PSRAM share that controller, so
 * sustained arena traffic starves anything fetching from flash - which
 * is most of the kernel - and it was what made the display collapse
 * when core1's scanout was moved into the XIP cache.  Bulk data that is
 * touched once belongs in the arena; a decoder's working set does not.
 */

static void *m_malloc(size_t n, void *u) { (void)u; return malloc(n); }
static void m_free(void *p, void *u) { (void)u; free(p); }
static void *m_realloc(void *p, size_t n, void *u)
{
	(void)u;
	return realloc(p, n);
}

/* ---- the decoder -------------------------------------------------------- */

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DR_MP3_NO_SIMD
#define DR_MP3_ONLY_MP3
#define DRMP3_DATA_CHUNK_SIZE 32768
#include "dr_mp3.h"

static int mfd = -1;			/* the MP3 file */

static size_t on_read(void *u, void *buf, size_t n)
{
	unsigned char *out = buf;
	size_t total = 0;
	int got;

	(void)u;
	while (total < n) {
		got = read(mfd, out + total, (int)(n - total));
		if (got <= 0)
			break;
		total += (size_t)got;
	}
	return total;
}

static drmp3_bool32 on_seek(void *u, int offset, drmp3_seek_origin origin)
{
	int whence = (origin == DRMP3_SEEK_CUR) ? SEEK_CUR
		   : (origin == DRMP3_SEEK_END) ? SEEK_END : SEEK_SET;

	(void)u;
	return lseek(mfd, (off_t)offset, whence) == (off_t)-1
		? DRMP3_FALSE : DRMP3_TRUE;
}

static drmp3_bool32 on_tell(void *u, drmp3_int64 *cursor)
{
	off_t here = lseek(mfd, 0, SEEK_CUR);

	(void)u;
	if (here == (off_t)-1)
		return DRMP3_FALSE;
	*cursor = (drmp3_int64)here;
	return DRMP3_TRUE;
}

static const drmp3_allocation_callbacks ALLOC = {
	NULL, m_malloc, m_realloc, m_free
};

/* ---- volume ------------------------------------------------------------- */

/*
 * MicroPython's law, not MMBasic's.  pcaudio's _compute_gain maps 0-100
 * onto an 8.8 fixed point gain over a ~50dB log taper,
 * 256 * 10^((v-100)/40), and audio.scale then applies it as
 * (sample * gain) >> 8.  MMBasic's i2sconvert is
 * sample * mapping[vol] / 2000 - a multiply AND A DIVIDE for every
 * sample of every frame.  Same curve, a shift instead of a divide, and
 * this is the per-sample path so it is the one that matters.
 *
 * Tabulated rather than computed so nothing here needs pow() and the
 * program does not drag in libm for one value.
 */
static const short vol_gain[101] = {
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

static void apply_gain(short *s, int n, int gain)
{
	int i;

	if (gain >= 256)
		return;				/* unity: nothing to do */
	if (gain <= 0) {
		memset(s, 0, (size_t)n * sizeof(short));
		return;
	}
	for (i = 0; i < n; i++)
		s[i] = (short)(((long)s[i] * gain) >> 8);
}

/* ---- playback ----------------------------------------------------------- */

/* 2048 frames is MMBasic's WAV_BUFFER_SIZE (8192 bytes stereo) - a
 * proven figure, and comfortably below the kernel ring's 256K. */
#define BLOCK 2048
static short pcm[BLOCK * 2];

static volatile int stopping;

static void on_intr(int sig)
{
	(void)sig;
	stopping = 1;			/* close tidily rather than leaving
					   1.5 seconds of ring playing on */
}

int main(int argc, char *argv[])
{
	drmp3 *mp3;
	struct snd_pcm cfg;
	struct snd_buf sb;
	struct snd_stat st;
	int sfd, vol = 80, gain, chans, i, n;
	drmp3_uint64 got;
	unsigned long frames = 0;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: %s file.mp3 [volume 0-100]\n", argv[0]);
		return 1;
	}
	if (argc == 3) {
		vol = atoi(argv[2]);
		if (vol < 0) vol = 0;
		if (vol > 100) vol = 100;
	}
	gain = vol_gain[vol];

	mfd = open(argv[1], O_RDONLY);
	if (mfd < 0) {
		perror(argv[1]);
		return 1;
	}

	mp3 = malloc(sizeof(drmp3));
	if (!drmp3_init(mp3, on_read, on_seek, on_tell, NULL, NULL, &ALLOC)) {
		fprintf(stderr, "playmp3: %s is not a valid MP3\n", argv[1]);
		return 1;
	}
	chans = (int)mp3->channels;

	sfd = open("/dev/sys", O_RDWR);
	if (sfd < 0) {
		perror("/dev/sys");
		return 1;
	}
	cfg.rate = mp3->sampleRate;
	cfg.channels = (unsigned short)chans;
	cfg.bits = 16;
	if (ioctl(sfd, SNDIOC_PCMOPEN, &cfg) < 0) {
		/* There is one I2S engine and one owner of it.  Say which of
		 * the two things went wrong: "busy" sent people looking at
		 * the sample rate for a fault that was a second player. */
		if (errno == EBUSY)
			fprintf(stderr, "playmp3: sound output in use by pid %d\n",
				ioctl(sfd, SNDIOC_PCMOWNER, 0));
		else
			fprintf(stderr, "playmp3: cannot start audio at %lu Hz\n",
				(unsigned long)mp3->sampleRate);
		return 1;
	}
	printf("%s: %lu Hz, %d channel%s, volume %d\n", argv[1],
	       (unsigned long)mp3->sampleRate, chans, chans == 1 ? "" : "s",
	       vol);

	signal(SIGINT, on_intr);

	while (!stopping) {
		int bytes;

		got = drmp3_read_pcm_frames_s16(mp3, BLOCK, pcm);
		if (got == 0)
			break;			/* end of file */
		frames += (unsigned long)got;
		n = (int)got * chans;		/* samples, not frames */
		apply_gain(pcm, n, gain);
		bytes = n * (int)sizeof(short);

		/* Hand it over as the ring has room.  A short write is the
		 * normal case once playback has caught up: the decoder runs
		 * at about three times real time, so most of this loop is
		 * spent waiting rather than decoding. */
		for (i = 0; i < bytes && !stopping; ) {
			sb.base = (char *)pcm + i;
			sb.len = (unsigned long)(bytes - i);
			n = ioctl(sfd, SNDIOC_PCMWRITE, &sb);
			if (n < 0) {
				perror("SNDIOC_PCMWRITE");
				stopping = 1;
				break;
			}
			if (n == 0)
				usleep(20000);
			i += n;
		}
	}

	/* Let the ring play out, unless we are stopping on a signal - then
	 * the point is to stop now.  CLOSE drops whatever is still queued.
	 * Bounded, so a driver fault cannot wedge the shell. */
	if (!stopping) {
		for (i = 0; i < 200; i++) {
			if (ioctl(sfd, SNDIOC_PCMSTAT, &st) < 0)
				break;
			if (st.queued == 0)
				break;
			usleep(20000);
		}
	}
	if (ioctl(sfd, SNDIOC_PCMSTAT, &st) == 0 && st.underruns)
		fprintf(stderr, "playmp3: %lu underruns\n",
			(unsigned long)st.underruns);

	ioctl(sfd, SNDIOC_PCMCLOSE, 0);
	close(sfd);
	close(mfd);
	return 0;
}

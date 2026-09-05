/* playmod - the PLAY MODFILE player, and MODSAMPLE's mixer.
 *
 *   playmod file.mod [volume 0-100] [noloop]
 *
 * hxcmod (vendored via the MicroPython PC3 port, seffect extension
 * included) rendering at 22050 Hz stereo into the kernel's PCM
 * stream - the reference's modfilesamplerate, met here by running the
 * I2S at that rate rather than doubling samples.  Spawned by PLAY
 * MODFILE (mmb_play.h); PLAY MODSAMPLE arrives over the control FIFO
 * while the music plays and becomes hxcmod_playsoundeffect - one of
 * four effect slots pointed at a sample ALREADY IN THE FILE, mixed
 * into the ongoing render, music uninterrupted.  PLAY STOP is the
 * same SIGINT playmp3 catches.  With noloop the song plays once, the
 * ring drains, and exit hands the audio owner slot back - which is
 * how the BASIC side's completion interrupt sees the end.
 *
 * The FILE lives in the PSRAM arena, not the process: hxcmod plays
 * pattern and sample data in place, a .mod can be bigger than the
 * process pool wants to give, and sample reads are sequential runs -
 * the QMI-friendly placement (the MP3 plan measured the other
 * arrangements).  The WORKING SET (modcontext, the render buffer)
 * stays in the process image.  Integer only, no FPU flags.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "../pico_ioctl.h"
#include "pc3sys.h"
#include "hxcmod.h"
#include "mmb_playctl.h"

#define RATE 22050
#define CHUNK 1024		/* frames per render */
/*
 *	How much audio to keep queued, and it MUST outlast the sleep
 *	below - which is where this was wrong.
 *
 *	usleep() rounds UP to deciseconds, because the kernel timer wheel
 *	runs once per 100ms (Library/libs/usleep.c): usleep(20000) does
 *	not sleep 20ms, it sleeps 100ms.  With 2048*4 bytes queued -
 *	92.9ms at 22050Hz stereo - this player refilled, slept PAST THE
 *	END of its own buffer, and woke to silence.  That was the ~10Hz
 *	pulsing on PETSCII Robots' title screen: about a tenth of a
 *	second of music, then about a tenth of nothing, forever.  It was
 *	never scheduling or swapping.
 *
 *	16384 bytes - 186ms - fixed the pulse with nothing else running
 *	and did NOT fix it with something else running, which is the
 *	measurement that matters: a pure-compute BASIC loop was enough to
 *	empty it.  After the 100ms sleep expires this still has to be
 *	SCHEDULED, and then render, while another process wants the
 *	processor; 86ms of margin does not cover that.
 *
 *	The players that do not pulse show what the margin should be.
 *	playmp3, playwav and playflac share pcm_write() in pcmplay.h and
 *	have NO target at all: they write until the ring refuses, which
 *	is up to the 256K the kernel holds - seconds of audio - and sleep
 *	only when it is genuinely full.  That is what "the kernel's deep
 *	ring" in playmp3's header is buying, and why an MP3 survives a
 *	busy machine that a MOD does not.
 *
 *	This does not go the whole way, because unlike a music file a MOD
 *	takes EFFECTS: queued audio plays before anything new, so the
 *	queue depth is the delay on a PLAY MODSAMPLE, and a game whose
 *	door thuds arrive a second late is worse than one that stutters.
 *	48K is 557ms - 457ms of margin after the sleep, which is five
 *	times what proved insufficient - and half a second of effect
 *	latency, which is poor but usable.
 *
 *	SO THE GUESS IS GONE.  SNDIOC_PCMWAIT sleeps in the kernel until
 *	the ring has drained to a mark, and the kernel wakes it on the
 *	TICK - 5ms, not the 100ms usleep can manage.  The queue now only
 *	has to cover 5ms of granularity instead of a decisecond of sleep
 *	plus a scheduling delay, so it can be short again, and short is
 *	exactly what a game's sound effects need.
 *
 *	8192 bytes is 92.9ms of music in hand and 92.9ms before a door
 *	thud is heard - back to where this started, but now for a reason
 *	rather than by accident, and without the dropouts, because the
 *	waiting is done by the thing that knows when the ring drains.
 *	The low mark is half of it: refill when half is gone.
 */
#define TARGET_BYTES 8192	/* ~93 ms queued */
#define LOW_BYTES    4096	/* ~46 ms: wake and top up */

static modcontext modctx;
static short pcm[CHUNK * 2];
static volatile int stopping;

static void on_intr(int sig)
{
	(void)sig;
	stopping = 1;
}

/* playmp3's taper, verbatim - one loudness law for every player */
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

static int gain = 136;			/* volume 80 */

int main(int argc, char *argv[])
{
	struct snd_pcm cfg;
	struct snd_buf sb;
	struct snd_stat st;
	struct psram_req rq;
	struct mm_playmsg m;
	unsigned char *mod;
	FILE *f;
	long size;
	int sfd, ffd, n, i, noloop = 0, ended = 0;

	if (argc < 2) {
		fprintf(stderr,
			"usage: playmod file.mod [volume] [noloop]\n");
		return 1;
	}
	if (argc >= 3) {
		int v = atoi(argv[2]);

		if (v < 0)
			v = 0;
		if (v > 100)
			v = 100;
		gain = vol_gain[v];
	}
	if (argc >= 4)
		noloop = atoi(argv[3]) != 0;

	f = fopen(argv[1], "rb");
	if (f == NULL) {
		perror(argv[1]);
		return 1;
	}
	fseek(f, 0L, SEEK_END);
	size = ftell(f);
	fseek(f, 0L, SEEK_SET);

	sfd = pc3_open_sys();
	if (sfd < 0) {
		perror("/dev/sys");
		return 1;
	}
	rq.len = (unsigned long)size;
	if (pc3_ioctl(sfd, PSRAMIOC_ALLOC, &rq) < 0) {
		fprintf(stderr, "playmod: no room for %ld bytes\n", size);
		return 1;
	}
	mod = (unsigned char *)rq.base;
	if (fread(mod, 1, (size_t)size, f) != (size_t)size) {
		fprintf(stderr, "playmod: short read\n");
		return 1;
	}
	fclose(f);

	if (!hxcmod_init(&modctx) ||
	    !hxcmod_setcfg(&modctx, RATE, 1, 1) ||
	    !hxcmod_load(&modctx, mod, (int)size)) {
		fprintf(stderr, "playmod: %s is not a MOD file\n", argv[1]);
		return 1;
	}

	/* FIFO before the stream, as playsnd: the client's owner poll
	 * is the ready signal, so everything must exist first */
	unlink(MM_PLAYCTL_FIFO);
	if (mkfifo(MM_PLAYCTL_FIFO, 0666) == 0)
		ffd = open(MM_PLAYCTL_FIFO, O_RDWR | O_NDELAY);
	else
		ffd = -1;

	cfg.rate = RATE;
	cfg.channels = 2;
	cfg.bits = 16;
	if (pc3_ioctl(sfd, SNDIOC_PCMOPEN, &cfg) < 0) {
		fprintf(stderr, "playmod: sound output in use\n");
		unlink(MM_PLAYCTL_FIFO);
		return 1;
	}

	/* kind for a later program's adoption, as playsnd */
	{
		FILE *kf = fopen(MM_PLAY_KINDFILE, "w");

		if (kf != NULL) {
			fputc('M', kf);
			fclose(kf);
		}
	}

	signal(SIGINT, on_intr);

	while (!stopping) {
		if (ffd >= 0)
			while ((n = read(ffd, &m, sizeof(m))) ==
			       (int)sizeof(m)) {
				if (m.ver != MM_PLAYCTL_VER)
					continue;
				if (m.op == MM_PLAY_MODSAMP)
					hxcmod_playsoundeffect(&modctx,
					    (unsigned short)(m.a - 1),
					    (unsigned short)(m.b - 1),
					    (unsigned char)(m.p1 - 1),
					    3579545 / 16000);
				else if (m.op == MM_PLAY_VOLUME) {
					int v = m.p1 > m.p2 ? m.p1 : m.p2;

					if (v < 0)
						v = 0;
					if (v > 100)
						v = 100;
					gain = vol_gain[v];
				}
			}
		if (pc3_ioctl(sfd, SNDIOC_PCMSTAT, &st) < 0)
			break;
		while (st.queued < TARGET_BYTES && !stopping && !ended) {
			int off = 0, w;

			memset(pcm, 0, sizeof(pcm));
			if (hxcmod_fillbuffer(&modctx, (msample *)pcm,
					      CHUNK, NULL, noloop) && noloop)
				ended = 1;
			for (i = 0; i < CHUNK * 2; i++)
				pcm[i] = (short)((pcm[i] * gain) >> 8);
			/* short writes are the normal case: hand the whole
			 * chunk over, as playmp3 does - dropping the tail
			 * was the starvation-gap bug */
			while (off < (int)sizeof(pcm) && !stopping) {
				sb.base = (char *)pcm + off;
				sb.len = (unsigned long)(sizeof(pcm) - off);
				w = pc3_ioctl(sfd, SNDIOC_PCMWRITE, &sb);
				if (w < 0) {
					stopping = 1;
					break;
				}
				if (w == 0 &&
				    pc3_ioctl(sfd, SNDIOC_PCMWAIT,
					  (void *)LOW_BYTES) < 0)
					usleep(1);
				off += w;
			}
			if (pc3_ioctl(sfd, SNDIOC_PCMSTAT, &st) < 0) {
				stopping = 1;
				break;
			}
		}
		if (ended && st.queued == 0)
			break;
		/* Sleep until the ring has drained to the low mark, woken on
		   the kernel tick.  This is the whole reason the queue above
		   can be 93ms instead of 557ms: no guess at how long to wait,
		   so no need to keep half a second of latency in hand. */
		if (pc3_ioctl(sfd, SNDIOC_PCMWAIT, (void *)LOW_BYTES) < 0) {
			static int moaned;
			if (!moaned) {
				moaned = 1;
				fprintf(stderr, "playmod: PCMWAIT failed, errno %d"
					" - falling back to a 100ms sleep\n",
					errno);
			}
			usleep(1);
		}
	}

	pc3_ioctl(sfd, SNDIOC_PCMCLOSE, 0);
	unlink(MM_PLAYCTL_FIFO);
	unlink(MM_PLAY_KINDFILE);
	return 0;
}

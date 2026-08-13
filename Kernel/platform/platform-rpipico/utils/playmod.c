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
#include "hxcmod.h"
#include "mmb_playctl.h"

#define RATE 22050
#define CHUNK 1024		/* frames per render */
#define TARGET_BYTES (2048 * 4)	/* ~93 ms queued */

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

	sfd = open("/dev/sys", O_RDWR);
	if (sfd < 0) {
		perror("/dev/sys");
		return 1;
	}
	rq.len = (unsigned long)size;
	if (ioctl(sfd, PSRAMIOC_ALLOC, &rq) < 0) {
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
	if (ioctl(sfd, SNDIOC_PCMOPEN, &cfg) < 0) {
		fprintf(stderr, "playmod: sound output in use\n");
		unlink(MM_PLAYCTL_FIFO);
		return 1;
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
		if (ioctl(sfd, SNDIOC_PCMSTAT, &st) < 0)
			break;
		while (st.queued < TARGET_BYTES && !stopping && !ended) {
			memset(pcm, 0, sizeof(pcm));
			if (hxcmod_fillbuffer(&modctx, (msample *)pcm,
					      CHUNK, NULL, noloop) && noloop)
				ended = 1;
			for (i = 0; i < CHUNK * 2; i++)
				pcm[i] = (short)((pcm[i] * gain) >> 8);
			sb.base = pcm;
			sb.len = sizeof(pcm);
			if (ioctl(sfd, SNDIOC_PCMWRITE, &sb) < 0)
				break;
			if (ioctl(sfd, SNDIOC_PCMSTAT, &st) < 0)
				break;
		}
		if (ended && st.queued == 0)
			break;
		usleep(20000);
	}

	ioctl(sfd, SNDIOC_PCMCLOSE, 0);
	unlink(MM_PLAYCTL_FIFO);
	return 0;
}

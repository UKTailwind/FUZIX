/* playsnd - the PLAY SOUND / PLAY TONE synthesiser daemon.
 *
 * The MicroPython PC3 port's synth core (ports/rp2/audio.c, itself
 * MMBasic's) rendered into the kernel's PCM stream: 4 voices x 2
 * sides, sine/square/triangle/saw/periodic/white noise from the
 * vendored MMBasic tables, 44100 Hz stereo 16-bit.  Spawned by the
 * first PLAY SOUND or PLAY TONE (mmb_play.h), commanded over the
 * control FIFO (mmb_playctl.h), stopped by PLAY STOP's SIGINT or by
 * five seconds of silence - either way the PCM stream (and with it
 * the audio-owner slot) is released for MP3 and MOD.
 *
 * INTEGER ONLY, deliberately: the reference uses float phase
 * accumulators, but this machine saves no FP context and permits
 * exactly two FPU programs (utils/Makefile says why).  A 20.12
 * fixed-point accumulator in table units puts the worst pitch error
 * at a quarter-millihertz - four orders below hearing - and keeps
 * this program out of that argument entirely.  TONE's whole-cycle
 * duration rounding needs doubles, so the CLIENT computes it (the
 * translated program has them) and sends a sample count.
 *
 * The feed loop is playmp3's proven shape: top up to a SHORT target
 * (2048 frames ~ 46 ms - the ring holds 1.5 s, but a full ring is
 * 1.5 s of latency on every SOUND change), usleep(20000), drain the
 * FIFO each pass.
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
#include "sound_tables.h"
#include "mmb_playctl.h"

#define RATE 44100
#define CHUNK 512		/* frames rendered per pass */
#define TARGET 2048		/* keep this many frames queued */
#define IDLE_EXIT_US 5000000L

/* voice state - audio.c's snd_voice_t with the phase in 20.12 */
struct voice {
	unsigned char type;	/* MM_SND_* */
	long phase;		/* table units << 12, wraps at 4096<<12 */
	long phinc;
	int vol;		/* current index into mapping[] 0..41 */
	int vol_target;
	long dwell;		/* white noise: samples left on this level */
	int noiseval;
};

static struct voice vc[4][2];
static unsigned short noisetable[4096];
static int noise_made;

/* the tone generator - full scale, its own phase pair */
static long tone_ph[2], tone_inc[2];
static long long tone_left = -2;	/* -2 off, -1 forever, else frames */

static int gain = 205;			/* master, 8.8: 80% of 256 */
static volatile int stopping;

static void on_intr(int sig)
{
	(void)sig;
	stopping = 1;
}

/* MMBasic's PLAY VOLUME curve is mapping[] itself; playmp3 uses a
 * squared taper.  One knob, one meaning: mapping[v] scaled to 8.8
 * against its own full scale, which is what i2sconvert does. */
static void set_gain(int left, int right)
{
	int v = left > right ? left : right;

	if (v < 0)
		v = 0;
	if (v > 100)
		v = 100;
	gain = (mapping[v] << 8) / mapping[100];
}

static void make_noise(void)
{
	int i;

	if (noise_made)
		return;
	noise_made = 1;
	for (i = 0; i < 4096; i++)
		noisetable[i] = (unsigned short)(rand() % 3800 + 100);
}

/* audio.c's snd_sample, fixed point */
static int snd_sample(struct voice *v)
{
	int j, ph = (int)(v->phase >> 12);

	switch (v->type) {
	case MM_SND_SINE:
		j = SineTable[ph];
		break;
	case MM_SND_TRI:
		j = triangletable[ph];
		break;
	case MM_SND_SQUARE:
		j = ph > 2047 ? 3900 : 100;
		break;
	case MM_SND_SAW:
		j = ph * 3800 / 4096 + 100;
		break;
	case MM_SND_PNOISE:
		j = noisetable[ph];
		break;
	case MM_SND_WNOISE:
		if (v->dwell <= 0) {
			v->dwell = v->phinc >> 12;
			if (v->dwell <= 0)
				v->dwell = 1;
			v->noiseval = rand() % 3800 + 100;
		}
		v->dwell--;
		return (v->noiseval - 2000) * mapping[v->vol] / 2000;
	default:
		return 0;
	}
	v->phase += v->phinc;
	if (v->phase >= (4096L << 12))
		v->phase -= (4096L << 12);
	return (j - 2000) * mapping[v->vol] / 2000;
}

static short pcm[CHUNK * 2];

/* the mixing loop of audio_sound_read + audio_tone_read, one pass */
static void render(int frames)
{
	static int ramp;
	int n, i, s;

	for (n = 0; n < frames; n++) {
		int lv = 0, rv = 0;

		if (++ramp >= 44) {	/* SOUND_RAMP_INTERVAL */
			ramp = 0;
			for (i = 0; i < 4; i++)
				for (s = 0; s < 2; s++) {
					struct voice *v = &vc[i][s];

					if (v->vol < v->vol_target)
						v->vol++;
					else if (v->vol > v->vol_target)
						v->vol--;
				}
		}
		if (tone_left != -2) {
			/* full-scale sine, the reference's *16 */
			lv = (SineTable[tone_ph[0] >> 12] - 2000) * 16;
			rv = (SineTable[tone_ph[1] >> 12] - 2000) * 16;
			tone_ph[0] += tone_inc[0];
			if (tone_ph[0] >= (4096L << 12))
				tone_ph[0] -= (4096L << 12);
			tone_ph[1] += tone_inc[1];
			if (tone_ph[1] >= (4096L << 12))
				tone_ph[1] -= (4096L << 12);
			if (tone_left > 0 && --tone_left == 0)
				tone_left = -2;
		} else {
			for (i = 0; i < 4; i++) {
				if (vc[i][0].type != MM_SND_OFF)
					lv += snd_sample(&vc[i][0]);
				if (vc[i][1].type != MM_SND_OFF)
					rv += snd_sample(&vc[i][1]);
			}
			lv *= 16;
			rv *= 16;
		}
		pcm[n * 2] = (short)((lv * gain) >> 8);
		pcm[n * 2 + 1] = (short)((rv * gain) >> 8);
	}
}

static int quiet(void)
{
	int i, s;

	if (tone_left != -2)
		return 0;
	for (i = 0; i < 4; i++)
		for (s = 0; s < 2; s++)
			if (vc[i][s].type != MM_SND_OFF || vc[i][s].vol != 0)
				return 0;
	return 1;
}

static void do_msg(struct mm_playmsg *m)
{
	int i;

	if (m->ver != MM_PLAYCTL_VER)
		return;
	switch (m->op) {
	case MM_PLAY_SOUND: {
		int voice = m->a - 1;
		long inc;

		if (voice < 0 || voice > 3)
			break;
		if (m->p1 == MM_SND_PNOISE)
			make_noise();
		/* phinc = freq/RATE * 4096 in 20.12: freq arrives in
		 * mHz, so inc = freq_mHz * 4096 * 4096 / (RATE * 1000).
		 * Factored to keep the 32-bit intermediate in range:
		 * (mHz / 125) * 4096 * 4096 / (RATE * 8) with the
		 * remainder folded back in. */
		{
			long long t = (long long)m->p2 << 24;

			inc = (long)(t / ((long long)RATE * 1000));
		}
		for (i = 0; i < 2; i++) {
			struct voice *v = &vc[voice][i];

			if (!(m->b & (1 << i)))
				continue;
			/* WNOISE reuses phinc as the dwell length in
			 * samples, exactly as the reference does */
			v->phinc = inc;
			if (m->p1 == MM_SND_WNOISE)
				v->dwell = 0;
			v->type = (unsigned char)m->p1;
			v->vol_target = (int)(m->p3 * 41 / 25);
			if (v->type == MM_SND_OFF)
				v->vol_target = 0;
			if (v->phase >= (4096L << 12))
				v->phase = 0;
		}
		break;
	}
	case MM_PLAY_TONE:
		tone_ph[0] = tone_ph[1] = 0;
		tone_inc[0] = (long)(((long long)m->p1 << 24) /
				     ((long long)RATE * 1000));
		tone_inc[1] = (long)(((long long)m->p2 << 24) /
				     ((long long)RATE * 1000));
		tone_left = (m->p3 < 0) ? -1 : (long long)m->p3;
		if (tone_left == 0)
			tone_left = -2;
		break;
	case MM_PLAY_VOLUME:
		set_gain(m->p1, m->p2);
		break;
	default:
		break;
	}
}

int main(int argc, char *argv[])
{
	struct snd_pcm cfg;
	struct snd_buf sb;
	struct snd_stat st;
	struct mm_playmsg m;
	int sfd, ffd, n;
	long idle = 0;

	if (argc >= 2)
		set_gain(atoi(argv[1]), atoi(argv[1]));

	/* FIFO first, then the stream: the client polls PCMOWNER and
	 * writes the moment it goes live, so the FIFO must already be
	 * there - and a fresh inode discards any stale records. */
	unlink(MM_PLAYCTL_FIFO);
	if (mkfifo(MM_PLAYCTL_FIFO, 0666) < 0) {
		perror("playsnd: mkfifo");
		return 1;
	}
	ffd = open(MM_PLAYCTL_FIFO, O_RDWR | O_NDELAY);
	if (ffd < 0) {
		perror("playsnd: fifo");
		return 1;
	}

	sfd = open("/dev/sys", O_RDWR);
	if (sfd < 0) {
		perror("/dev/sys");
		return 1;
	}
	cfg.rate = RATE;
	cfg.channels = 2;
	cfg.bits = 16;
	if (ioctl(sfd, SNDIOC_PCMOPEN, &cfg) < 0) {
		/* a lost spawn race, usually: someone else owns audio */
		fprintf(stderr, "playsnd: sound output in use\n");
		unlink(MM_PLAYCTL_FIFO);
		return 1;
	}

	signal(SIGINT, on_intr);

	while (!stopping) {
		while ((n = read(ffd, &m, sizeof(m))) == (int)sizeof(m)) {
			do_msg(&m);
			idle = 0;
		}
		if (ioctl(sfd, SNDIOC_PCMSTAT, &st) < 0)
			break;
		while (st.queued < TARGET * 4 && !stopping) {
			render(CHUNK);
			sb.base = pcm;
			sb.len = sizeof(pcm);
			if (ioctl(sfd, SNDIOC_PCMWRITE, &sb) < 0)
				break;
			if (ioctl(sfd, SNDIOC_PCMSTAT, &st) < 0)
				break;
		}
		if (quiet()) {
			idle += 20000;
			if (idle >= IDLE_EXIT_US)
				break;
		} else
			idle = 0;
		usleep(20000);
	}

	ioctl(sfd, SNDIOC_PCMCLOSE, 0);
	unlink(MM_PLAYCTL_FIFO);
	return 0;
}

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
#include "pc3sys.h"
#include "sound_tables.h"
#include "mmb_playctl.h"

#define RATE 44100
#define CHUNK 512		/* frames rendered per pass */
/* Keep ~186 ms queued.  MMBasic keeps 93, but this machine stalls its
 * processes for ~150 ms at a stretch on a slow heartbeat (pcmpace's
 * cushion scan: 168 underruns in 5 s at 16K, 17 at 24K, ZERO at 32K),
 * so 93 ms starves through no fault of the feeder.  32K rides every
 * stall out; the cost is SOUND changes landing ~186 ms late.  When the
 * stall itself is found and fixed, this can come back down. */
#define TARGET 8192		/* frames queued: ~186 ms at 44100 */
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

/* polyBLEP, as the kernel synth and the reference now render: a
 * square edge or saw wrap that can only land on a sample tick carries
 * alias images that beat against the true harmonics - the shimmer on
 * sustained notes.  Within one sample of the edge the band-limited
 * step differs from the naive one by (1-t)^2 of the step toward its
 * midpoint.  d and inc are 20.12 phase units; the result is q*q in
 * 0..65536, 65536 at the edge. */
static long blep_q(long d, long inc)
{
	long t;

	if (inc < 256)
		return 0;
	t = d / (inc >> 8);
	if (t >= 256)
		return 0;
	t = 256 - t;
	return t * t;
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
	case MM_SND_SQUARE: {
		long q = 0, half = 2048L << 12, full = 4096L << 12;

		j = ph > 2047 ? 3900 : 100;
		if (v->phase < v->phinc)
			q = blep_q(v->phase, v->phinc);
		else if (full - v->phase < v->phinc)
			q = blep_q(full - v->phase, v->phinc);
		else if (v->phase >= half && v->phase - half < v->phinc)
			q = blep_q(v->phase - half, v->phinc);
		else if (v->phase < half && half - v->phase < v->phinc)
			q = blep_q(half - v->phase, v->phinc);
		if (q)
			j = 2000 + (int)((long)(j - 2000) * (65536 - q) /
					 65536);
		break;
	}
	case MM_SND_SAW: {
		long full = 4096L << 12;

		j = ph * 3800 / 4096 + 100;
		if (v->phase < v->phinc)
			j += (int)(1900L * blep_q(v->phase, v->phinc) /
				   65536);
		else if (full - v->phase < v->phinc)
			j -= (int)(1900L * blep_q(full - v->phase,
						  v->phinc) / 65536);
		break;
	}
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
	int sfd, ffd, n, dbg = 0, nofifo = 0;
	long idle = 0;
	unsigned long last_under = 0;
	int statctr = 0;

	if (argc >= 2)
		set_gain(atoi(argv[1]), atoi(argv[1]));
	if (argc >= 3 && argv[2][0] == 'd')
		dbg = 1;
	/* 't': the FIFO-free discriminator.  A 440 sine baked in at
	 * start, no mkfifo, no control fd, no per-pass reads - everything
	 * else identical.  If this holds clean where the normal mode
	 * pulses, the per-pass FIFO read path is the destroyer. */
	if (argc >= 3 && argv[2][0] == 't') {
		int s;

		for (s = 0; s < 2; s++) {
			vc[0][s].type = MM_SND_SINE;
			vc[0][s].phinc = (long)(((long long)440000 << 24) /
						((long long)RATE * 1000));
			vc[0][s].vol = 41;
			vc[0][s].vol_target = 41;
		}
		nofifo = 1;
	}

	/* FIFO first, then the stream: the client polls PCMOWNER and
	 * writes the moment it goes live, so the FIFO must already be
	 * there - and a fresh inode discards any stale records. */
	if (nofifo)
		ffd = -1;
	else {
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
	}

	sfd = pc3_open_sys();
	if (sfd < 0) {
		perror("/dev/sys");
		return 1;
	}
	cfg.rate = RATE;
	cfg.channels = 2;
	cfg.bits = 16;
	if (pc3_ioctl(sfd, SNDIOC_PCMOPEN, &cfg) < 0) {
		/* a lost spawn race, usually: someone else owns audio */
		fprintf(stderr, "playsnd: sound output in use\n");
		unlink(MM_PLAYCTL_FIFO);
		return 1;
	}

	/* Who owns the stream, for the NEXT program: a client born after
	 * this daemon has mm_play_kind = NONE and needs to adopt rather
	 * than raise "Sound output in use" at a synth it could use. */
	{
		FILE *kf = fopen(MM_PLAY_KINDFILE, "w");

		if (kf != NULL) {
			fputc('S', kf);
			fclose(kf);
		}
	}

	signal(SIGINT, on_intr);

	while (!stopping) {
		for (; ffd >= 0;) {
			n = read(ffd, &m, sizeof(m));
			if (n != (int)sizeof(m)) {
				if (dbg && n > 0)
					fprintf(stderr,
						"snd: partial %d\n", n);
				break;
			}
			if (dbg)
				fprintf(stderr,
					"snd: op%d a%d b%d %ld %ld %ld\n",
					m.op, m.a, m.b, (long)m.p1,
					(long)m.p2, (long)m.p3);
			do_msg(&m);
			idle = 0;
		}
		if (pc3_ioctl(sfd, SNDIOC_PCMSTAT, &st) < 0)
			break;
		if (dbg) {
			if (st.underruns != last_under) {
				fprintf(stderr, "snd: UNDERRUN %lu (q %lu)\n",
					(unsigned long)st.underruns,
					(unsigned long)st.queued);
				last_under = st.underruns;
			}
			if (++statctr >= 50) {	/* ~1 s of passes */
				statctr = 0;
				fprintf(stderr, "snd: q %lu u %lu\n",
					(unsigned long)st.queued,
					(unsigned long)st.underruns);
			}
		}
		while (st.queued < TARGET * 4 && !stopping) {
			int off = 0, w;

			render(CHUNK);
			/* A short write is the NORMAL case (playmp3's
			 * comment says so): hand the chunk over piece by
			 * piece until the ring has taken all of it.
			 * Dropping the tail was the morse-code bug. */
			while (off < (int)sizeof(pcm) && !stopping) {
				sb.base = (char *)pcm + off;
				sb.len = (unsigned long)(sizeof(pcm) - off);
				w = pc3_ioctl(sfd, SNDIOC_PCMWRITE, &sb);
				if (w < 0) {
					stopping = 1;
					break;
				}
				if (w == 0)
					usleep(20000);
				off += w;
			}
			if (pc3_ioctl(sfd, SNDIOC_PCMSTAT, &st) < 0) {
				stopping = 1;
				break;
			}
			if (dbg)
				fprintf(stderr, "snd: w%d q%lu\n", off,
					(unsigned long)st.queued);
		}
		if (quiet()) {
			idle += 10000;
			if (idle >= IDLE_EXIT_US)
				break;
		} else
			idle = 0;
		usleep(10000);
	}

	pc3_ioctl(sfd, SNDIOC_PCMCLOSE, 0);
	unlink(MM_PLAYCTL_FIFO);
	unlink(MM_PLAY_KINDFILE);
	return 0;
}

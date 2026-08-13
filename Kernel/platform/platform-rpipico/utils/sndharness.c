/* sndharness - playsnd's voice renderer on the HOST, writing a WAV.
 *
 * Byte-identical copies of struct voice, blep_q, snd_sample and
 * render() from playsnd.c, driven the way t-mode drives them: voice 1
 * sine 440 both sides, vol 41, gain for volume 70.  If the board
 * pulses and this WAV is clean, the renderer is exonerated; if the
 * pulse is in this file, it is arithmetic and visible.
 *
 *   gcc -O1 -o sndharness sndharness.c && ./sndharness out.wav [hz]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sound_tables.h"
#include "mmb_playctl.h"

#define RATE 44100
#define CHUNK 512
#define SECONDS 5

/* ---- verbatim from playsnd.c ---- */

struct voice {
	unsigned char type;
	long phase;
	long phinc;
	int vol;
	int vol_target;
	long dwell;
	int noiseval;
};

static struct voice vc[4][2];
static unsigned short noisetable[4096];
static long tone_ph[2], tone_inc[2];
static long long tone_left = -2;
static int gain = 205;

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

static void render(int frames)
{
	static int ramp;
	int n, i, s;

	for (n = 0; n < frames; n++) {
		int lv = 0, rv = 0;

		if (++ramp >= 44) {
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

/* ---- host driver ---- */

static void wav_header(FILE *f, unsigned long nframes)
{
	unsigned long datalen = nframes * 4, riff = 36 + datalen;
	unsigned char h[44];

	memcpy(h, "RIFF", 4);
	h[4] = riff; h[5] = riff >> 8; h[6] = riff >> 16; h[7] = riff >> 24;
	memcpy(h + 8, "WAVEfmt ", 8);
	h[16] = 16; h[17] = h[18] = h[19] = 0;
	h[20] = 1; h[21] = 0;		/* PCM */
	h[22] = 2; h[23] = 0;		/* stereo */
	h[24] = RATE & 255; h[25] = (RATE >> 8) & 255; h[26] = h[27] = 0;
	{
		unsigned long br = RATE * 4;

		h[28] = br; h[29] = br >> 8; h[30] = br >> 16; h[31] = br >> 24;
	}
	h[32] = 4; h[33] = 0;
	h[34] = 16; h[35] = 0;
	memcpy(h + 36, "data", 4);
	h[40] = datalen; h[41] = datalen >> 8;
	h[42] = datalen >> 16; h[43] = datalen >> 24;
	fwrite(h, 1, 44, f);
}

int main(int argc, char *argv[])
{
	FILE *f;
	int hz = argc >= 3 ? atoi(argv[2]) : 440;
	unsigned long total = (unsigned long)RATE * SECONDS, done = 0;
	int s;

	if (argc < 2) {
		fprintf(stderr, "usage: sndharness out.wav [hz]\n");
		return 1;
	}
	for (s = 0; s < 2; s++) {
		vc[0][s].type = MM_SND_SINE;
		vc[0][s].phinc = (long)(((long long)hz * 1000 << 24) /
					((long long)RATE * 1000));
		vc[0][s].vol = 41;
		vc[0][s].vol_target = 41;
	}
	gain = (mapping[70] << 8) / mapping[100];

	f = fopen(argv[1], "wb");
	if (f == NULL) {
		perror(argv[1]);
		return 1;
	}
	wav_header(f, total);
	while (done < total) {
		render(CHUNK);
		fwrite(pcm, 4, CHUNK, f);
		done += CHUNK;
	}
	fclose(f);
	printf("%s: %d Hz, %d s\n", argv[1], hz, SECONDS);
	return 0;
}

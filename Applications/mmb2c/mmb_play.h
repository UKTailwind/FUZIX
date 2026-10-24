#ifndef MMB_PLAY_H
#define MMB_PLAY_H
/*
 *	PLAY SOUND / PLAY TONE - the client half.
 *
 *	The synthesiser is a separate process (utils/playsnd.c) holding
 *	the kernel's one PCM stream, spawned by the first SOUND or TONE
 *	and commanded over the control FIFO (mmb_playctl.h).  This
 *	header is the BASIC side: argument checks at the reference's
 *	ranges, the spawn-and-discover dance, and TONE's whole-cycle
 *	duration rounding - done HERE, with doubles the daemon does not
 *	carry, and sent as a sample count.
 *
 *	Discovery is the kernel's SNDIOC_PCMOWNER (via mm_play_owner):
 *	a live owner plus a matching mm_play_kind means the daemon is
 *	up; a live owner with the WRONG kind is the reference's "sound
 *	output in use"; no owner means spawn and poll until the stream
 *	goes live.  PLAY STOP needs nothing new - the daemon catches
 *	the same SIGINT playmp3 does.
 *
 *	The tone-done interrupt costs no IPC at all: the duration is
 *	known at issue, so a DEADLINE is kept here and mmb_int.h's poll
 *	fires the handler when the clock passes it.
 */

#include <stdio.h>
#include "mmb_playctl.h"

#ifndef MMG_FN
#if defined(MM_FCC) || defined(MM_PC3)
#define MMG_FN static
#else
#define MMG_FN static __inline__ __attribute__((unused))
#endif
#endif

/* the one volume, shared with PLAY MP3's spawn argument.  Defined here
 * when this header is present; the translator emits its own copy only
 * for programs whose PLAY is MP3-alone. */
static int mm_play_volume = 80;

#define MMP_KIND_NONE 0
#define MMP_KIND_SND  1
#define MMP_KIND_MOD  2
#define MMP_KIND_MP3  3
static int mm_play_kind;

/* the tone deadline, in mm_us() microseconds; 0 = no interrupt armed */
static long long mm_tone_end;

/* The record itself goes through the runtime (mm_play_send): the
 * program side carries no file descriptors, and the FIFO mechanics
 * live once, next to the spawn machinery they belong with. */
#define mmp_send(op, a, b, p1, p2, p3) 	mm_play_send((op), (a), (b), (long)(p1), (long)(p2), (long)(p3))

/* A stream owner this program did not start is not necessarily "in
 * use": a synth daemon from the previous RUN is still commandable
 * over the FIFO.  The daemon says what it is in the kind file; adopt
 * that, and only a genuine mismatch (an MP3 player writes no kind
 * file) raises. */
MMG_FN void mmp_adopt(void)
{
	FILE *kf;
	int c;

	if (mm_play_kind != MMP_KIND_NONE || mm_play_owner() == 0)
		return;
	kf = fopen(MM_PLAY_KINDFILE, "r");
	if (kf == NULL)
		return;
	c = fgetc(kf);
	fclose(kf);
	if (c == 'S')
		mm_play_kind = MMP_KIND_SND;
	else if (c == 'M')
		mm_play_kind = MMP_KIND_MOD;
}

/* The daemon is up, or becomes up, or this raises.  kind is what the
 * caller needs running; a live stream of another kind is the
 * reference's error. */
MMG_FN int mmp_ensure(int kind, const char *prog)
{
	int i;

	if (mm_play_owner() != 0) {
		mmp_adopt();
		if (mm_play_kind == kind)
			return 0;
		MM_RAISEV("Sound output in use", -1);
	}
	mm_play_kind = MMP_KIND_NONE;
	mm_run_begin();
	mm_run_arg(prog);
	mm_run_arg_i(mm_play_volume);
	if (mm_play_start() < 0)
		return -1;
	for (i = 0; i < 100; i++) {
		if (mm_play_owner() != 0) {
			mm_play_kind = kind;
			return 0;
		}
		mm_pause(20);
	}
	MM_RAISEV("Sound output did not start", -1);
}

/* The channel and the type when the program works them out as it runs
 * - MMBasic's getCstring/strcasecmp arm (Audio.c:1977 and 2085), which
 * is why "b" and "B" both do, and why a variable is allowed where the
 * manual only shows a letter.  A letter written literally never
 * reaches these: the translator decodes it.
 *
 * The messages are the reference's own. */
MMG_FN int mmp_side(const char *s)
{
	int c = mm_slen(s) == 1 ? mm_cstr(s)[0] : 0;

	if (c >= 'a' && c <= 'z')
		c -= 32;
	if (c == 'L')
		return 1;
	if (c == 'R')
		return 2;
	if (c == 'B' || c == 'M')
		return 3;
	MM_RAISEV("Position must be L, R, or B", 3);
}

MMG_FN int mmp_type(const char *s)
{
	int c = mm_slen(s) == 1 ? mm_cstr(s)[0] : 0;

	if (c >= 'a' && c <= 'z')
		c -= 32;
	switch (c) {
	case 'O': return MM_SND_OFF;
	case 'S': return MM_SND_SINE;
	case 'Q': return MM_SND_SQUARE;
	case 'T': return MM_SND_TRI;
	case 'W': return MM_SND_SAW;
	case 'P': return MM_SND_PNOISE;
	case 'N': return MM_SND_WNOISE;
	case 'U':
		/* a user-defined wave table, which nothing here can load */
		MM_RAISEV("PLAY SOUND type U is not translated", MM_SND_OFF);
	}
	MM_RAISEV("Invalid type", MM_SND_OFF);
}

/* PLAY SOUND voice, channel, type [, frequency [, volume]] -
 * cmd_play's own ranges (Audio.c:1946). */
MMG_FN void mmp_sound(MMINTEGER voice, MMINTEGER sides, MMINTEGER type,
		      MMFLOAT freq, MMINTEGER vol)
{
	if (voice < 1 || voice > 4)
		MM_RAISE("Invalid sound number");
	if (type != MM_SND_OFF && (freq < 1 || freq > 20000))
		MM_RAISE("Invalid frequency");
	if (vol < 0 || vol > 25)
		MM_RAISE("Invalid volume");
	/* The kernel synthesiser first: one ioctl, audible within a
	 * 64-frame buffer, no daemon and no second process.  -2 is an
	 * MP3/MOD player holding the output; -1 is a kernel without the
	 * ioctl (or the host), and the daemon path below keeps every
	 * already-shipped arrangement working there. */
	{
		MMINTEGER r = mm_snd_cmd(MM_PLAY_SOUND, voice, sides, type,
					 (MMINTEGER)(freq * 1000.0 + 0.5),
					 vol);

		if (r == 0) {
			mm_play_kind = MMP_KIND_SND;
			return;
		}
		if (r == -2)
			MM_RAISE("Sound output in use");
	}
	if (mmp_ensure(MMP_KIND_SND, "\007playsnd") < 0)
		return;
	if (mmp_send(MM_PLAY_SOUND, (int)voice, (int)sides, (long)type,
		     (long)(freq * 1000.0 + 0.5), (long)vol) < 0)
		MM_RAISE("Sound output did not start");
}

/* PLAY TONE left, right [, duration_ms [, interrupt]] - frequencies
 * 0-20000; the duration is rounded DOWN to whole left-channel cycles
 * when the frequency reaches 10 Hz, ending on a zero crossing, which
 * is the reference's anti-click (audio.c:377-399).  No duration means
 * forever. */
MMG_FN void mmp_tone(MMFLOAT fl, MMFLOAT fr, MMFLOAT ms, MMINTEGER has_int)
{
	long long samples;

	if (fl < 0 || fl > 20000 || fr < 0 || fr > 20000)
		MM_RAISE("Invalid frequency");
	if (ms < 0)
		MM_RAISE("Invalid duration");
	if (ms == 0)
		samples = -1;
	else {
		MMFLOAT frames = ms * 44.1;

		if (fl >= 10.0) {
			MMFLOAT cyc = (MMFLOAT)(long long)(frames * fl /
							   44100.0);

			if (cyc < 1)
				cyc = 1;
			frames = cyc * (44100.0 / fl);
		}
		samples = (long long)frames;
		if (samples < 1)
			samples = 1;
	}
	/* Kernel first, as mmp_sound - the tone-done interrupt needs no
	 * transport either way, so it is shared below. */
	{
		MMINTEGER r = mm_snd_cmd(MM_PLAY_TONE, 0, 0,
					 (MMINTEGER)(fl * 1000.0 + 0.5),
					 (MMINTEGER)(fr * 1000.0 + 0.5),
					 (MMINTEGER)(samples < 0 ? -1
							       : samples));

		if (r == 0) {
			mm_play_kind = MMP_KIND_SND;
			goto tone_int;
		}
		if (r == -2)
			MM_RAISE("Sound output in use");
	}
	if (mmp_ensure(MMP_KIND_SND, "\007playsnd") < 0)
		return;
	if (mmp_send(MM_PLAY_TONE, 0, 0, (long)(fl * 1000.0 + 0.5),
		     (long)(fr * 1000.0 + 0.5),
		     (long)(samples < 0 ? -1 : samples)) < 0)
		MM_RAISE("Sound output did not start");
tone_int:
	if (has_int && samples > 0)
		mm_tone_end = (long long)mm_us() +
		    (long long)((MMFLOAT)samples * (1000000.0 / 44100.0));
	else
		mm_tone_end = 0;
}

/* PLAY MODFILE file$ [, interrupt] - playmod loads the whole file
 * (into the PSRAM arena, its comment says why) and plays at 22050;
 * with an interrupt the song runs once and the daemon's exit IS the
 * completion signal - mmb_int.h polls the owner away.  A live stream
 * of any kind refuses, the reference's way: PLAY STOP first. */
MMG_FN void mmp_modfile(const char *file, MMINTEGER has_int)
{
	int i;

	if (mm_play_owner() != 0)
		MM_RAISE("Sound output in use");
	mm_play_kind = MMP_KIND_NONE;
	mm_run_begin();
	mm_run_arg("\007playmod");
	mm_run_arg(file);
	mm_run_arg_i(mm_play_volume);
	mm_run_arg_i(has_int ? 1 : 0);
	if (mm_play_start() < 0)
		return;
	for (i = 0; i < 100; i++) {
		if (mm_play_owner() != 0) {
			mm_play_kind = MMP_KIND_MOD;
			return;
		}
		mm_pause(20);
	}
	MM_RAISE("Could not play the MOD file");
}

/* PLAY MODSAMPLE sample, channel [, volume] - one record to the
 * RUNNING player: hxcmod points an effect slot at a sample already in
 * the file and mixes it over the music.  Nothing is loaded, nothing
 * pauses.  The reference requires a MOD to be playing, and so does
 * this - the owner check plus the kind is its CurrentlyPlaying ==
 * P_MOD. */
MMG_FN void mmp_modsample(MMINTEGER s, MMINTEGER ch, MMINTEGER vol)
{
	if (s < 1 || s > 32)
		MM_RAISE("Invalid sample number");
	if (ch < 1 || ch > 4)
		MM_RAISE("Invalid channel");
	if (vol < 1 || vol > 64)
		MM_RAISE("Invalid volume");
	mmp_adopt();
	if (mm_play_owner() == 0 || mm_play_kind != MMP_KIND_MOD)
		MM_RAISE("Samples play over MOD file");
	if (mmp_send(MM_PLAY_MODSAMP, (int)s, (int)ch, (long)vol, 0, 0) < 0)
		MM_RAISE("Samples play over MOD file");
}

/* PLAY BBC SOUND channel, amplitude, pitch, duration - the BBC Micro's
 * SOUND (Audio.c:1958), on the kernel's BBC synthesiser.  The
 * reference's ranges: the channel word &HSFC 0-&HFFFF, amplitude -15 to
 * 0 or an envelope 1-16, pitch and duration 0-255.  A player holding
 * the output is its "Sound output in use"; a channel whose queue is
 * full is waited out, as the BBC waited.  With no sound device (the
 * host gates) nothing plays. */
MMG_FN void mmp_bbc_sound(MMINTEGER ch, MMINTEGER amp, MMINTEGER pitch,
			  MMINTEGER dur)
{
	if (mm_range(ch, 0, 0xFFFF) || mm_range(amp, -15, 16)
	    || mm_range(pitch, 0, 255) || mm_range(dur, 0, 255))
		return;
	mmp_adopt();
	if (mm_play_owner() != 0)
		MM_RAISE("Sound output in use");
	while (mm_bbc_sound(ch, amp, pitch, dur) == 1)
		mm_pause(20);
}

/* PLAY BBC ENVELOPE n, T, PI1, PI2, PI3, PN1, PN2, PN3, AA, AD, AS, AR,
 * ALA, ALD (Audio.c:2023), each in the reference's range, as 14 bytes
 * packed two integers wide for the runtime. */
MMG_FN void mmp_bbc_env(MMINTEGER n, MMINTEGER t, MMINTEGER pi1,
			MMINTEGER pi2, MMINTEGER pi3, MMINTEGER pn1,
			MMINTEGER pn2, MMINTEGER pn3, MMINTEGER aa,
			MMINTEGER ad, MMINTEGER as, MMINTEGER ar,
			MMINTEGER ala, MMINTEGER ald)
{
	static const short lim[14][2] = {
		{1, 16}, {0, 255}, {-128, 127}, {-128, 127}, {-128, 127},
		{0, 255}, {0, 255}, {0, 255}, {-127, 127}, {-127, 127},
		{-127, 127}, {-127, 127}, {0, 126}, {0, 126}
	};
	MMINTEGER v[14];
	MMINTEGER lo = 0, hi = 0;
	int i;

	v[0] = n; v[1] = t; v[2] = pi1; v[3] = pi2; v[4] = pi3;
	v[5] = pn1; v[6] = pn2; v[7] = pn3; v[8] = aa; v[9] = ad;
	v[10] = as; v[11] = ar; v[12] = ala; v[13] = ald;
	for (i = 0; i < 14; i++)
		if (mm_range(v[i], lim[i][0], lim[i][1]))
			return;
	for (i = 0; i < 8; i++)
		lo |= (v[i] & 0xFF) << (8 * i);
	for (i = 0; i < 6; i++)
		hi |= (v[8 + i] & 0xFF) << (8 * i);
	mm_bbc_env(lo, hi);
}

/* PLAY VOLUME for a program that also plays SOUND/TONE/MOD: remember
 * it for the next spawn AND tell the daemon that is running now. */
MMG_FN void mmp_volume(MMINTEGER v)
{
	mm_play_volume = (int)v;
	if (mm_play_volume < 0)
		mm_play_volume = 0;
	if (mm_play_volume > 100)
		mm_play_volume = 100;
	if (mm_play_kind == MMP_KIND_SND) {
		mm_snd_cmd(MM_PLAY_VOLUME, 0, 0, mm_play_volume,
			   mm_play_volume, 0);
		return;
	}
	if (mm_play_owner() != 0)
		mmp_send(MM_PLAY_VOLUME, 0, 0, mm_play_volume,
			 mm_play_volume, 0);
}

#endif /* MMB_PLAY_H */

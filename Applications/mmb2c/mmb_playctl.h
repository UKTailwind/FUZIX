#ifndef MMB_PLAYCTL_H
#define MMB_PLAYCTL_H
/*
 *	The player control FIFO - the one protocol between mmb_play.h
 *	(the client inside every translated program) and the player
 *	daemons (utils/playsnd.c, utils/playmod.c).
 *
 *	This header exists IN BOTH TREES (mmb2c and Applications/CC),
 *	synced like mmb_runtime.c, and the version byte in every record
 *	is what turns a stale copy into an error instead of a silent
 *	skew - the mmb_gfx_map lesson, applied in advance.
 *
 *	The kernel semantics the protocol leans on were pinned on the
 *	board by utils/fifotest.c: every open carries O_NDELAY (a FIFO
 *	open without it psleeps once, whoever you are); the daemon holds
 *	the FIFO O_RDWR so its read end never sees EOF; an empty-pipe
 *	read returns 0, which is "quiet"; and writes cannot signal a dead
 *	daemon (they buffer), so discovery is SNDIOC_PCMOWNER - the
 *	daemon holds the PCM stream for as long as it lives.
 */

#define MM_PLAYCTL_FIFO "/tmp/.playctl"
/* 2: PLAY MODSAMPLE's rate, pan and LOOP, and its CHANGE and STOP
 * (V8.00.00b2) - a version 1 player would take the LOOP bit for a
 * channel and write past its effect slots. */
#define MM_PLAYCTL_VER  2

/* One byte, 'S' or 'M', written by the daemon once it owns the PCM
 * stream and unlinked when it exits: how a program born AFTER the
 * daemon learns what is playing (its own mm_play_kind starts NONE).
 * MMBasic's CurrentlyPlaying, made to survive a program boundary. */
#define MM_PLAY_KINDFILE "/tmp/.playkind"

struct mm_playmsg {
	unsigned char ver;	/* MM_PLAYCTL_VER */
	unsigned char op;
	unsigned char a, b;
	int p1, p2, p3;		/* int is 32 bits on every target here,
				   where long is 64 on the x64 host */
};

#define MM_PLAY_SOUND	1	/* a=voice 1-4, b=side bits (L=1,R=2),
				   p1=type, p2=freq mHz, p3=vol 0-25   */
#define MM_PLAY_TONE	2	/* p1=left mHz, p2=right mHz,
				   p3=duration in SAMPLES (-1 forever) */
#define MM_PLAY_MODSAMP	3	/* a=sample 1-31, b=channel 1-4, with
				   MM_PLAY_LOOP for LOOP; p1=vol 0-64,
				   p2=rate 150-500000, p3=pan -100-100 */
#define MM_PLAY_VOLUME	4	/* p1=left 0-100, p2=right 0-100       */
#define MM_PLAY_MODCHG	5	/* MODSAMPLE CHANGE: b=channel 1-4,
				   p1=vol 0-64 or -1, p2=rate or 0,
				   p3=pan or MM_PLAY_PANKEEP (kept)    */
#define MM_PLAY_MODSTOP	6	/* MODSAMPLE STOP: b=channel 1-4, 0 all */

#define MM_PLAY_LOOP	0x80	/* in MODSAMP's b */
#define MM_PLAY_PANKEEP	1000	/* hxcmod's HXCMOD_PAN_KEEP */

/* PLAY SOUND types - the reference's letters, as numbers */
#define MM_SND_OFF	0
#define MM_SND_SINE	1
#define MM_SND_SQUARE	2
#define MM_SND_TRI	3
#define MM_SND_SAW	4
#define MM_SND_PNOISE	5
#define MM_SND_WNOISE	6

#endif /* MMB_PLAYCTL_H */

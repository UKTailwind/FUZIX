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

#include <fcntl.h>

/* Everything that is not the codec - the volume law, the kernel sink,
 * the drain, the signal, the process-heap allocator and WHY it is the
 * process heap and not the PSRAM arena.  playwav and playflac include
 * the same file; this used to be written out here and would now be
 * written out three times. */
#include "pcmplay.h"

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

static size_t on_read(void *u, void *buf, size_t n)
{
	(void)u;
	return pcm_fd_read(buf, n);
}

static drmp3_bool32 on_seek(void *u, int offset, drmp3_seek_origin origin)
{
	int whence = (origin == DRMP3_SEEK_CUR) ? SEEK_CUR
		   : (origin == DRMP3_SEEK_END) ? SEEK_END : SEEK_SET;

	(void)u;
	return pcm_fd_seek(offset, whence) ? DRMP3_TRUE : DRMP3_FALSE;
}

static drmp3_bool32 on_tell(void *u, drmp3_int64 *cursor)
{
	off_t here = lseek(pcm_fd, 0, SEEK_CUR);

	(void)u;
	if (here == (off_t)-1)
		return DRMP3_FALSE;
	*cursor = (drmp3_int64)here;
	return DRMP3_TRUE;
}

/* dr_mp3 is the one of the three that does want realloc. */
static const drmp3_allocation_callbacks ALLOC = {
	NULL, pcm_malloc, m_realloc, pcm_free
};

/* ---- playback ----------------------------------------------------------- */

int main(int argc, char *argv[])
{
	drmp3 *mp3;
	int sfd, vol = 80, gain, chans, n;
	drmp3_uint64 got;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: %s file.mp3 [volume 0-100]\n", argv[0]);
		return 1;
	}
	if (argc == 3)
		vol = pcm_volume_arg(argv[2]);
	gain = pcm_vol_gain[vol];

	pcm_fd = open(argv[1], O_RDONLY);
	if (pcm_fd < 0) {
		perror(argv[1]);
		return 1;
	}

	mp3 = malloc(sizeof(drmp3));
	if (mp3 == NULL) {
		fprintf(stderr, "playmp3: no room for the decoder (%u bytes)\n",
			(unsigned)sizeof(drmp3));
		return 1;
	}
	if (!drmp3_init(mp3, on_read, on_seek, on_tell, NULL, NULL, &ALLOC)) {
		fprintf(stderr, "playmp3: %s is not a valid MP3\n", argv[1]);
		return 1;
	}
	chans = (int)mp3->channels;

	sfd = pcm_open("playmp3", (unsigned long)mp3->sampleRate, chans);
	if (sfd < 0)
		return 1;
	printf("%s: %lu Hz, %d channel%s, volume %d\n", argv[1],
	       (unsigned long)mp3->sampleRate, chans, chans == 1 ? "" : "s",
	       vol);

	while (!pcm_stopping) {
		got = drmp3_read_pcm_frames_s16(mp3, PCM_BLOCK, pcm_buf);
		if (got == 0)
			break;			/* end of file */
		n = (int)got * chans;		/* samples, not frames */
		pcm_apply_gain(pcm_buf, n, gain);
		if (pcm_write(sfd, pcm_buf, n) < 0)
			break;
	}

	pcm_close("playmp3", sfd);
	close(pcm_fd);
	return 0;
}

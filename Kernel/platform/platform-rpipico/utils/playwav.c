/*
 * playwav - play a WAV file through the PCM5102.
 *
 *   playwav file.wav [volume]        volume 0-100, default 80
 *
 * playmp3's shape exactly - see pcmplay.h, which holds everything that
 * is not the codec - with dr_wav in place of dr_mp3.
 *
 * dr_wav is MMBasic's copy (third_party_mod/dr_wav.h, v0.14.0),
 * configured as Audio.c configures it.  drwav_read_pcm_frames_s16 does
 * the format conversion, so this plays whatever dr_wav understands -
 * 8/16/24/32-bit PCM, IEEE float, A-law and mu-law - and hands the sink
 * the 16-bit it wants.
 *
 * SOFT FLOAT, unlike playmp3.  16-bit PCM is the common case and is a
 * copy; the float and law conversions are per-sample but not a DSP
 * inner loop.  That keeps this out of the "exactly one process with
 * live FP state" argument entirely (pcmplay.h says why that matters).
 */

#include <fcntl.h>

#include "pcmplay.h"

/* ---- the decoder ------------------------------------------------------- */

#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#define DR_WAV_NO_SIMD
#include "dr_wav.h"

static size_t on_read(void *u, void *buf, size_t n)
{
	(void)u;
	return pcm_fd_read(buf, n);
}

static drwav_bool32 on_seek(void *u, int offset, drwav_seek_origin origin)
{
	int whence = (origin == drwav_seek_origin_current) ? SEEK_CUR : SEEK_SET;

	(void)u;
	return pcm_fd_seek(offset, whence) ? DRWAV_TRUE : DRWAV_FALSE;
}

/* onRealloc NULL: dr_wav does not need it for reading, and MMBasic
 * passes NULL here too. */
static const drwav_allocation_callbacks ALLOC = {
	NULL, pcm_malloc, NULL, pcm_free
};

int main(int argc, char *argv[])
{
	drwav *wav;
	int sfd, vol = 80, gain, chans, n;
	drwav_uint64 got;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: %s file.wav [volume 0-100]\n", argv[0]);
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

	/* The drwav struct is the caller's here (unlike drflac, which
	 * allocates its own), and it is far too big for the stack. */
	wav = malloc(sizeof(drwav));
	if (wav == NULL) {
		fprintf(stderr, "playwav: no room for the decoder (%u bytes)\n",
			(unsigned)sizeof(drwav));
		return 1;
	}
	if (!drwav_init(wav, on_read, on_seek, NULL, &ALLOC)) {
		fprintf(stderr, "playwav: %s is not a WAV this can read\n",
			argv[1]);
		return 1;
	}
	chans = (int)wav->channels;

	sfd = pcm_open("playwav", (unsigned long)wav->sampleRate, chans);
	if (sfd < 0)
		return 1;
	printf("%s: %lu Hz, %d channel%s, %u-bit, volume %d\n", argv[1],
	       (unsigned long)wav->sampleRate, chans, chans == 1 ? "" : "s",
	       (unsigned)wav->bitsPerSample, vol);

	while (!pcm_stopping) {
		got = drwav_read_pcm_frames_s16(wav, PCM_BLOCK, pcm_buf);
		if (got == 0)
			break;                  /* end of file */
		n = (int)got * chans;           /* samples, not frames */
		pcm_apply_gain(pcm_buf, n, gain);
		if (pcm_write(sfd, pcm_buf, n) < 0)
			break;
	}

	pcm_close("playwav", sfd);
	drwav_uninit(wav);
	close(pcm_fd);
	return 0;
}

/*
 * playflac - play a FLAC file through the PCM5102.
 *
 *   playflac file.flac [volume]      volume 0-100, default 80
 *
 * playmp3's shape - see pcmplay.h, which holds everything that is not
 * the codec - with dr_flac in place of dr_mp3.
 *
 * dr_flac is MMBasic's copy (third_party_mod/dr_flac.h, v0.12.44),
 * configured as Audio.c configures it: NO_STDIO, NO_CRC, NO_SIMD,
 * NO_OGG.  NO_CRC is MMBasic's choice and is kept - the frame CRC costs
 * real time on this processor and a corrupt frame is audible either way.
 *
 * SOFT FLOAT, unlike playmp3.  FLAC is an integer codec: the LPC
 * predictor and residual decoding are all fixed point, so there is no
 * DSP float loop to lose, and this stays out of the "exactly one
 * process with live FP state" argument (pcmplay.h says why).
 *
 * ---- MEMORY, which is the part that differs from the other two ------
 *
 * drflac_open ALLOCATES THE DECODER ITSELF and sizes it from the file's
 * STREAMINFO, where drmp3 and drwav have a fixed struct the caller
 * provides.  The size is roughly
 *
 *      sizeof(drflac) + maxBlockSize * 4 * channels
 *
 * so a 4096-block stereo file wants about 38K and a 16384-block stereo
 * file wants about 132K - out of a 336K pool shared with everything
 * else on the machine.  A big-block FLAC can therefore fail to open on
 * a machine where a small-block one plays, which would otherwise look
 * like "FLAC does not work".
 *
 * So the allocator here COUNTS, and the failure says the number.  That
 * is the whole change from playmp3's plain malloc wrapper: an
 * out-of-memory that names the block size and the file is a fact, where
 * a null return is a mystery.
 */

#include <fcntl.h>

#include "pcmplay.h"

/* ---- the decoder ------------------------------------------------------- */

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_CRC
#define DR_FLAC_NO_SIMD
#define DR_FLAC_NO_OGG
#include "dr_flac.h"

static size_t on_read(void *u, void *buf, size_t n)
{
	(void)u;
	return pcm_fd_read(buf, n);
}

static drflac_bool32 on_seek(void *u, int offset, drflac_seek_origin origin)
{
	int whence = (origin == drflac_seek_origin_current) ? SEEK_CUR : SEEK_SET;

	(void)u;
	return pcm_fd_seek(offset, whence) ? DRFLAC_TRUE : DRFLAC_FALSE;
}

/* The counting allocator.  dr_flac asks for one big block and a couple
 * of small ones; remembering the largest request is enough to explain a
 * failure, and costs nothing per call. */
static unsigned long flac_asked;        /* largest single request */
static int flac_failed;                 /* a request came back NULL */

static void *flac_malloc(size_t n, void *u)
{
	void *p;

	if ((unsigned long)n > flac_asked)
		flac_asked = (unsigned long)n;
	p = pcm_malloc(n, u);
	if (p == NULL)
		flac_failed = 1;
	return p;
}

/* onRealloc NULL, as MMBasic passes: dr_flac does not need it to read,
 * and growing a decoder's working buffer is the last thing this pool
 * wants to be asked for. */
static const drflac_allocation_callbacks ALLOC = {
	NULL, flac_malloc, NULL, pcm_free
};

int main(int argc, char *argv[])
{
	drflac *flac;
	int sfd, vol = 80, gain, chans, n;
	drflac_uint64 got;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: %s file.flac [volume 0-100]\n", argv[0]);
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

	flac = drflac_open(on_read, on_seek, NULL, &ALLOC);
	if (flac == NULL) {
		/* Two very different failures wearing one NULL. */
		if (flac_failed)
			fprintf(stderr, "playflac: no room for the decoder - it "
				"wanted %lu bytes for %s\n", flac_asked, argv[1]);
		else
			fprintf(stderr, "playflac: %s is not a FLAC this can "
				"read\n", argv[1]);
		return 1;
	}
	chans = (int)flac->channels;

	sfd = pcm_open("playflac", (unsigned long)flac->sampleRate, chans);
	if (sfd < 0) {
		drflac_close(flac);
		return 1;
	}
	printf("%s: %lu Hz, %d channel%s, %u-bit, volume %d\n", argv[1],
	       (unsigned long)flac->sampleRate, chans, chans == 1 ? "" : "s",
	       (unsigned)flac->bitsPerSample, vol);

	while (!pcm_stopping) {
		got = drflac_read_pcm_frames_s16(flac, PCM_BLOCK, pcm_buf);
		if (got == 0)
			break;                  /* end of file */
		n = (int)got * chans;           /* samples, not frames */
		pcm_apply_gain(pcm_buf, n, gain);
		if (pcm_write(sfd, pcm_buf, n) < 0)
			break;
	}

	pcm_close("playflac", sfd);
	drflac_close(flac);
	close(pcm_fd);
	return 0;
}

/*
 * mp3bench - can this machine decode an MP3 in real time?
 *
 *   mp3bench file.mp3 [seconds]
 *
 * Decode only.  Nothing is played, no I2S is touched, no kernel sound
 * ioctl is called; the answer wanted here is one number, and mixing the
 * output stage into it would only muddy it.
 *
 * The number is the REALTIME RATIO: seconds of audio decoded divided by
 * seconds spent decoding.  It has to be comfortably over 1.0, because
 * on the real thing the same process also reads the SD card, copies
 * into the ring and competes with the display.  1.05 is not a pass.
 *
 * Why this exists (PC3-MP3-PLAN.md): minimp3, inside dr_mp3, is single
 * precision float in its inner loops.  Userland here is built
 * -mcpu=cortex-m33 with the SOFT-FLOAT ABI and no -mfpu, and the kernel
 * grants only CP4 - the double-precision DCP - so the M33's single
 * precision FPU is switched off entirely.  Every float operation in the
 * decoder is therefore a libgcc call today.  MicroPython and MMBasic
 * both play MP3s on this exact chip, but their SDK builds have the FPU
 * on.  So before any of the player is designed around the answer, we
 * measure: if soft float is fast enough there is nothing to do, and if
 * it is not, the plan says what turning the FPU on costs.
 *
 * Run it BOTH ways to make the comparison the plan needs:
 *
 *	make mp3bench				 (soft float, as shipped)
 *	make mp3bench FPU=1			 (-mfpu=fpv5-sp-d16)
 *
 * FPU=1 needs a kernel that grants CP10/CP11, or the first float
 * instruction takes a UsageFault and the program dies immediately -
 * which is itself a clear enough result.
 *
 * File reading IS included in the timing.  That is deliberate: it is
 * what the player will pay too.  The read volume is small next to the
 * decode (a 128kbit stream is 16KB/s) but it is not nothing on SD.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>

/* ---- the PSRAM arena ------------------------------------------------
 *
 * The decoder does not fit a process: the drmp3 struct alone is about
 * 16K (pcmFrames is sizeof(float) * DRMP3_MAX_SAMPLES_PER_FRAME = 9216
 * bytes on its own) and dr_mp3 keeps a 32K input chunk beside it.  So
 * it goes where cc2's and mmbc's tables go - one arena allocation, bump
 * allocated, released by the kernel on exit (PC3-PSRAM-ARENA.md).  The
 * player will do exactly the same, so this exercises that path too.
 *
 * HOSTBUILD swaps the arena for plain malloc so the same file builds
 * and runs on Linux:
 *
 *	gcc -DHOSTBUILD -O2 -o mp3bench.host mp3bench.c
 *
 * That matters more than it looks.  When the board said "not a valid
 * MP3 file" there were two candidates - a file damaged in transit, and
 * this program being wired up wrong - and no way to tell them apart
 * from the board alone.  The host build decides it: if the same source
 * decodes the same file on Linux, the wiring is right.
 */
#ifdef HOSTBUILD

#define ARENA_LEN 0
static unsigned char *ar_cur, *ar_end;		/* only for the report */

static void *ar_malloc(size_t n, void *u) { (void)u; return malloc(n); }
static void ar_free(void *p, void *u) { (void)u; free(p); }
static void *ar_realloc(void *p, size_t n, void *u)
{
	(void)u;
	return realloc(p, n);
}

#else

#define PSRAMIOC_ALLOC	0x000A

struct psram_req {
	unsigned long len;
	unsigned long base;
};

#define ARENA_LEN (128UL * 1024)

static unsigned char *ar_cur, *ar_end, *ar_last;

static void ar_init(void)
{
	struct psram_req rq;
	int fd;

	if (ar_cur != NULL)
		return;
	fd = open("/dev/sys", O_RDWR);
	rq.len = ARENA_LEN;
	if (fd < 0 || ioctl(fd, PSRAMIOC_ALLOC, &rq) < 0 || !rq.base) {
		fprintf(stderr, "mp3bench: no PSRAM arena (kernel without "
				"PSRAMIOC_ALLOC?)\n");
		exit(1);
	}
	close(fd);
	ar_cur = (unsigned char *)rq.base;
	ar_end = ar_cur + ARENA_LEN;
}

/* Bump allocation with a size header, and extend-in-place for the most
 * recent block - which is the only one dr_mp3 ever grows, because it
 * reallocs its input chunk and allocates nothing else after it.  Any
 * other realloc copies; nothing frees. */

static void *ar_malloc(size_t n, void *u)
{
	unsigned char *p;

	(void)u;
	ar_init();
	n = (n + 7) & ~(size_t)7;
	if (ar_cur + n + 8 > ar_end) {
		fprintf(stderr, "mp3bench: arena exhausted (wanted %lu)\n",
			(unsigned long)n);
		exit(2);
	}
	p = ar_cur;
	*(size_t *)p = n;
	ar_cur += n + 8;
	ar_last = p + 8;
	return ar_last;
}

static void ar_free(void *p, void *u)
{
	(void)p;
	(void)u;
}

static void *ar_realloc(void *p, size_t n, void *u)
{
	size_t old;
	void *np;

	if (p == NULL)
		return ar_malloc(n, u);
	old = *(size_t *)((unsigned char *)p - 8);
	n = (n + 7) & ~(size_t)7;
	if (p == ar_last) {			/* grow in place */
		if ((unsigned char *)p + n > ar_end) {
			fprintf(stderr, "mp3bench: arena exhausted on grow\n");
			exit(2);
		}
		*(size_t *)((unsigned char *)p - 8) = n;
		ar_cur = (unsigned char *)p + n;
		return p;
	}
	np = ar_malloc(n, u);
	memcpy(np, p, old < n ? old : n);
	return np;
}

#endif /* HOSTBUILD */

/* ---- the decoder ----------------------------------------------------
 *
 * Configured as MMBasic configures it in Audio.c - ONLY_MP3, NO_SIMD,
 * NO_STDIO, a 32K input chunk - and 16 bit output, not float.
 */

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DR_MP3_NO_SIMD
#define DR_MP3_ONLY_MP3
#define DRMP3_DATA_CHUNK_SIZE 32768
#include "dr_mp3.h"

static int fd;

/* Traced during drmp3_init only.  "not a valid MP3 file" says nothing
 * about WHICH of read, seek and tell misbehaved, and on a platform this
 * unusual that is the whole question. */
static int tracing;

static size_t on_read(void *u, void *buf, size_t n)
{
	unsigned char *out = buf;
	size_t total = 0;
	int got = 0;

	(void)u;
	while (total < n) {
		got = read(fd, out + total, (int)(n - total));
		if (got <= 0)
			break;
		total += (size_t)got;
	}
	if (tracing)
		fprintf(stderr, "  read %lu -> %lu (last %d)\n",
			(unsigned long)n, (unsigned long)total, got);
	return total;
}

/* dr_mp3 uses all three origins: a VBR or MPEG-2 file is sized by
 * seeking to the end, so collapsing END onto SET breaks init on exactly
 * the files most likely to be tried first. */
static drmp3_bool32 on_seek(void *u, int offset, drmp3_seek_origin origin)
{
	int whence = (origin == DRMP3_SEEK_CUR) ? SEEK_CUR
		   : (origin == DRMP3_SEEK_END) ? SEEK_END : SEEK_SET;
	off_t r;

	(void)u;
	r = lseek(fd, (off_t)offset, whence);
	if (tracing)
		fprintf(stderr, "  seek %d whence %d -> %ld\n",
			offset, whence, (long)r);
	return r == (off_t)-1 ? DRMP3_FALSE : DRMP3_TRUE;
}

static drmp3_bool32 on_tell(void *u, drmp3_int64 *cursor)
{
	off_t here = lseek(fd, 0, SEEK_CUR);

	(void)u;
	if (tracing)
		fprintf(stderr, "  tell -> %ld\n", (long)here);
	if (here == (off_t)-1)
		return DRMP3_FALSE;
	*cursor = (drmp3_int64)here;
	return DRMP3_TRUE;
}

static const drmp3_allocation_callbacks ALLOC = {
	NULL, ar_malloc, ar_realloc, ar_free
};

/* The same thing out of the process heap instead of the arena.  The
 * decoder's input buffer is what read() writes into, and in the arena
 * that is PSRAM; "-m" puts it in ordinary process memory so the two
 * cases can be told apart in one sitting rather than two. */
static void *m_malloc(size_t n, void *u) { (void)u; return malloc(n); }
static void m_free(void *p, void *u) { (void)u; free(p); }
static void *m_realloc(void *p, size_t n, void *u)
{
	(void)u;
	return realloc(p, n);
}

static const drmp3_allocation_callbacks MALLOC_ALLOC = {
	NULL, m_malloc, m_realloc, m_free
};

/* One MP3 frame of stereo PCM, which is the decoder's natural
 * granularity and the block the player will hand to the kernel. */
#define FRAME_PCM 1152
static drmp3_int16 pcm[FRAME_PCM * 2];

/*
 * The self-test that found the INT_MAX bug lived here: it read 32K in
 * one call, compared it against the same range read 512 bytes at a
 * time, and then called minimp3's drmp3dec_decode_frame directly.  All
 * three passed while dr_mp3 above them said "not a valid MP3 file",
 * which is what narrowed the fault to the wrapper and from there to
 * limits.h.  Removed once it had done that: it cost 64K of BSS, which
 * is real money against a 340K process pool.  The finding is recorded
 * in limits.h and PC3-MP3-PLAN.md.
 */

int main(int argc, char *argv[])
{
	drmp3 *mp3;
	clock_t start, elapsed;
	unsigned long frames = 0, calls = 0;
	unsigned long limit;
	unsigned long audio_ms, real_ms, ratio100;
	drmp3_uint64 got;
	int seconds = 30;
	int usemalloc = 0, dotrace = 0;
	int a;

	if (argc < 2 || argc > 5) {
		fprintf(stderr, "usage: %s file.mp3 [seconds] [-m] [-t]\n",
			argv[0]);
		fprintf(stderr, "  -m  process heap instead of the PSRAM arena\n");
		fprintf(stderr, "  -t  trace every read/seek/tell during init\n");
		return 1;
	}
	for (a = 2; a < argc; a++) {
		if (argv[a][0] == '-' && argv[a][1] == 'm')
			usemalloc = 1;
		else if (argv[a][0] == '-' && argv[a][1] == 't')
			dotrace = 1;
		else
			seconds = atoi(argv[a]);
	}
	if (seconds <= 0)
		seconds = 30;

	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		perror(argv[1]);
		return 1;
	}

	/* Size and first bytes, before the decoder is given a chance to
	 * have an opinion.  "not a valid MP3 file" on its own cannot tell
	 * a damaged file from a wrongly wired decoder, and that ambiguity
	 * cost a round trip.  A real MP3 starts either with "ID3" or with
	 * a frame sync, FF Ex/Fx. */
	{
		unsigned char h[16];
		int n, i;
		long len = (long)lseek(fd, 0, SEEK_END);
		long audio = 0;

		lseek(fd, 0, SEEK_SET);
		n = read(fd, h, sizeof(h));
		printf("%s: %ld bytes, starts", argv[1], len);
		for (i = 0; i < n; i++)
			printf(" %02x", h[i]);
		if (n >= 3 && h[0] == 'I' && h[1] == 'D' && h[2] == '3') {
			/* syncsafe: seven bits per byte */
			audio = 10 + (((long)h[6] << 21) | ((long)h[7] << 14)
				    | ((long)h[8] << 7) | (long)h[9]);
			printf("  (ID3v2.%u, audio at %ld)\n", h[3], audio);
		} else if (n >= 2 && h[0] == 0xFF && (h[1] & 0xE0) == 0xE0) {
			printf("  (frame sync at 0)\n");
		} else {
			printf("  (NEITHER ID3 NOR A FRAME SYNC)\n");
		}

		/* The first four bytes of audio ARE the MPEG frame header, and
		 * every field that decides whether dr_mp3 will accept the file
		 * is in them. */
		lseek(fd, audio, SEEK_SET);
		n = read(fd, h, sizeof(h));
		printf("at %ld:", audio);
		for (i = 0; i < n; i++)
			printf(" %02x", h[i]);
		if (n >= 4 && h[0] == 0xFF && (h[1] & 0xE0) == 0xE0) {
			static const char *ver[4] = { "2.5", "?", "2", "1" };
			printf("  sync ok, MPEG%s layer %u\n",
			       ver[(h[1] >> 3) & 3], 4 - ((h[1] >> 1) & 3));
		} else {
			printf("  NO FRAME SYNC HERE\n");
		}
		lseek(fd, 0, SEEK_SET);
	}

	mp3 = usemalloc ? malloc(sizeof(drmp3)) : ar_malloc(sizeof(drmp3), NULL);
	if (mp3 == NULL) {
		fprintf(stderr, "mp3bench: no room for the decoder\n");
		return 1;
	}
	printf("decoder in %s\n", usemalloc ? "process memory" : "the PSRAM arena");
	tracing = dotrace;
	if (!drmp3_init(mp3, on_read, on_seek, on_tell, NULL, NULL,
			usemalloc ? &MALLOC_ALLOC : &ALLOC)) {
		/* The object survives the failure, and every branch that can
		 * return leaves its fingerprints in it: whether a frame was
		 * ever decoded (mp3FrameSampleRate), whether it thought it
		 * had hit the end (atEnd), and whether it got as far as
		 * trying to grow the input buffer (dataCapacity). */
		fprintf(stderr, "mp3bench: drmp3_init failed\n");
		fprintf(stderr, "  atEnd %d  dataSize %lu  cap %lu  consumed %lu\n",
			(int)mp3->atEnd, (unsigned long)mp3->dataSize,
			(unsigned long)mp3->dataCapacity,
			(unsigned long)mp3->dataConsumed);
		fprintf(stderr, "  frame: %lu Hz, %lu ch   streamLen %lu  start %lu\n",
			(unsigned long)mp3->mp3FrameSampleRate,
			(unsigned long)mp3->mp3FrameChannels,
			(unsigned long)mp3->streamLength,
			(unsigned long)mp3->streamStartOffset);
		return 1;
	}
	tracing = 0;

	printf("%s: %lu Hz, %u channel%s\n", argv[1],
	       (unsigned long)mp3->sampleRate, (unsigned)mp3->channels,
	       mp3->channels == 1 ? "" : "s");
	printf("decoder state %lu bytes\n", (unsigned long)sizeof(drmp3));

	/* How many PCM frames make up the requested run of audio. */
	limit = (unsigned long)seconds * mp3->sampleRate;

	start = clock();
	while (frames < limit) {
		got = drmp3_read_pcm_frames_s16(mp3, FRAME_PCM, pcm);
		if (got == 0)
			break;			/* end of file */
		frames += (unsigned long)got;
		calls++;
	}
	elapsed = clock() - start;

	/*
	 * All integer.  Fuzix's printf has no %f - it prints a literal "f"
	 * and then the varargs are out of step for everything after it,
	 * which turned the first good run of this program into a page of
	 * nonsense that looked like a decode failure.  Nothing here may
	 * use floating point in a format string.
	 */
	audio_ms = (unsigned long)((frames / (mp3->sampleRate / 100)) * 10);
	real_ms  = (unsigned long)elapsed * 1000UL / (unsigned long)CLOCKS_PER_SEC;
	if (real_ms == 0)
		real_ms = 1;			/* too fast to time */
	ratio100 = audio_ms * 100UL / real_ms;

	printf("decoded %lu frames = %lu.%02lus of audio in %lu.%02lus\n",
	       frames, audio_ms / 1000, (audio_ms % 1000) / 10,
	       real_ms / 1000, (real_ms % 1000) / 10);
	printf("realtime ratio %lu.%02lux   (%lu MP3 frames/s over %lu calls)\n",
	       ratio100 / 100, ratio100 % 100,
	       calls * 1000UL / real_ms, calls);

	/* 44100 stereo needs 38.3 MP3 frames a second sustained.  Say so
	 * plainly rather than leaving it to be worked out from the ratio. */
	if (ratio100 >= 200)
		printf("VERDICT: comfortable\n");
	else if (ratio100 >= 130)
		printf("VERDICT: real time, but with little headroom\n");
	else if (ratio100 >= 100)
		printf("VERDICT: marginal - will glitch under load\n");
	else
		printf("VERDICT: NOT real time\n");

	close(fd);
	return 0;
}

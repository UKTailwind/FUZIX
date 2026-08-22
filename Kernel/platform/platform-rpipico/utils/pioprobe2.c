/*
 *	One emission per PROCESS, so every run is cold: which ingredient
 *	starves the stream?  ./pioprobe2 <mode>
 *	  1  FIN counter, 8000 words, pure busy-poll (never sleeps)
 *	  2  FIN counter, 8000 words, sleep(1) mid-stream
 *	  3  CIN counter, 2000 words, sleep(1) mid-stream
 *	  4  CIN counter, 2000 words, busy-poll
 *	If only the sleepers starve, the DMA is reading a process image
 *	that is not resident while its owner sleeps.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/pc3io.h>

#define SIG_PIN	2
#define CNT_PIN	4

static int gfd;
static unsigned long *bs;	/* the kernel's PSRAM word buffer */

static int cnt(int req, int pin, long arg, long long *val)
{
	struct cntreq cr;
	int r;

	memset(&cr, 0, sizeof(cr));
	cr.pin = pin;
	cr.arg = arg;
	if (val)
		cr.val = *val;
	r = ioctl(gfd, req, &cr);
	if (val)
		*val = cr.val;
	return r;
}

int main(int argc, char *argv[])
{
	long long v = 0, t0;
	unsigned long mark[6];
	int mode, n, i;

	mode = argc > 1 ? atoi(argv[1]) : 1;
	n = (mode <= 2) ? 8000 : 2000;

	gfd = open("/dev/gpio", O_RDWR);
	if (gfd >= 0) {
		struct pioout_buf pb;
		if (ioctl(gfd, GPIOC_PIOOUT_BUF, &pb) < 0) {
			perror("buf");
			return 1;
		}
		bs = (unsigned long *)pb.addr;
	}
	if (gfd < 0 || pc3_claim(PLK_PIN, SIG_PIN) ||
	    pc3_claim(PLK_PIN, CNT_PIN) ||
	    pc3_claim(PLK_PIO, PIOOUT_PLK_IDX) ||
	    pc3_claim(PLK_DMA, PIOOUT_DMA_CH)) {
		perror("setup");
		return 1;
	}
	pc3_pioout_pin(SIG_PIN);
	pc3_pioout_setup(PIOOUT_ORG_BS, 3, SIG_PIN, 0, 0);
	if (mode <= 2)
		cnt(GPIOC_CNT_FIN, CNT_PIN, 500, NULL);
	else
		cnt(GPIOC_CNT_CIN, CNT_PIN, 3, NULL);
	for (i = 0; i < n; i++)
		bs[i] = ((250UL * 20 - 3) << 1) | ((i & 1) ^ 1);

	pc3_pioout_start(bs, (unsigned long)n);
	if (mode == 2 || mode == 3)
		sleep(1);
	/* sample DMA progress each ~500ms of spin, no prints, no sleeps */
	t0 = pc3_us64();
	i = 0;
	while (pc3_pioout_busy() && pc3_us64() - t0 < 3000000L) {
		long long el = pc3_us64() - t0;
		if (i < 6 && el > (i + 1) * 500000L)
			mark[i++] = PC3_REG(PC3_DMA_COUNT);
	}
	while (i < 6)
		mark[i++] = PC3_REG(PC3_DMA_COUNT);

	cnt(GPIOC_CNT_READ, CNT_PIN, 0, &v);
	printf("mode %d: n=%d busy=%d read=%lld remaining@.5s/1s/1.5s/2s: "
	       "%lu %lu %lu %lu\n",
	       mode, n, pc3_pioout_busy(), v,
	       mark[0], mark[1], mark[2], mark[3]);
	if (pc3_pioout_busy()) {
		PC3_REG(PC3_DMA + 0x464) = 1UL << PIOOUT_DMA_CH;
		pc3_pioout_stop();
	}
	return 0;
}

/*
 *	Diagnose the second-start failure pioouttest exposed: the first
 *	stream is perfect, the next one emits its FIFO burst and stalls.
 *	Same rig (GP2 -> GP4).  Prints the DMA and SM registers at every
 *	step so the difference between run 1 and run 2 is visible.
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
static unsigned long bs[8000];

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

static void regs(const char *tag)
{
	printf("  %s: dmactrl %08lx count %lu fdebug %08lx addr %lu\n",
	       tag,
	       PC3_REG(PC3_DMA_CTRL),
	       PC3_REG(PC3_DMA_COUNT),
	       PC3_REG(PC3_PIO1_FDEBUG),
	       PC3_REG(PC3_PIO1 + 0x0D4 + 0x18UL * PIOOUT_SM));
}

static void run(const char *tag, int n, int abort_first)
{
	long long v = 0, t0;
	int i;

	for (i = 0; i < n; i++)
		bs[i] = ((250UL * 20 - 3) << 1) | ((i & 1) ^ 1);

	if (abort_first)
		PC3_REG(PC3_DMA + 0x464) = 1UL << PIOOUT_DMA_CH; /* CHAN_ABORT */

	pc3_pioout_setup(PIOOUT_ORG_BS, 3, SIG_PIN, 0, 0);
	cnt(GPIOC_CNT_CIN, CNT_PIN, 3, NULL);
	regs("before");
	pc3_pioout_start(bs, (unsigned long)n);
	t0 = pc3_us64();
	while (pc3_pioout_busy() && pc3_us64() - t0 < 2000000L)
		;
	regs("after ");
	cnt(GPIOC_CNT_READ, CNT_PIN, 0, &v);
	printf("%s: %d edges -> counted %lld, busy=%d\n",
	       tag, n, v, pc3_pioout_busy());
}

int main(void)
{
	gfd = open("/dev/gpio", O_RDWR);
	if (gfd < 0) {
		perror("/dev/gpio");
		return 1;
	}
	if (pc3_claim(PLK_PIN, SIG_PIN) || pc3_claim(PLK_PIN, CNT_PIN) ||
	    pc3_claim(PLK_PIO, PIOOUT_PLK_IDX) ||
	    pc3_claim(PLK_DMA, PIOOUT_DMA_CH)) {
		perror("claim");
		return 1;
	}
	pc3_pioout_pin(SIG_PIN);

	run("run1", 2000, 0);
	run("run2", 2000, 0);	/* the failure shape, back to back */
	run("run3", 2000, 1);	/* does an abort first cure it? */
	run("run4", 400, 0);	/* and once more without */

	/* run5: pioouttest T2's shape - SLEEP during the emission.  If
	   the process's memory does not stay put while it sleeps, the
	   DMA reads someone else's bytes: the count goes wrong, the
	   stream crawls (garbage durations are huge), and the words in
	   bs[] afterwards may not be the words we wrote. */
	{
		long long v = 0, t0;
		int i, bad = 0;

		for (i = 0; i < 2000; i++)
			bs[i] = ((500UL * 20 - 3) << 1) | ((i & 1) ^ 1);
		pc3_pioout_setup(PIOOUT_ORG_BS, 3, SIG_PIN, 0, 0);
		cnt(GPIOC_CNT_CIN, CNT_PIN, 3, NULL);
		regs("before");
		pc3_pioout_start(bs, 2000);	/* 1s of stream */
		sleep(1);
		regs("mid   ");
		printf("  read_addr %08lx (bs at %08lx..%08lx)\n",
		       PC3_REG(PC3_DMA_READ), (unsigned long)bs,
		       (unsigned long)(bs + 2000));
		t0 = pc3_us64();
		while (pc3_pioout_busy() && pc3_us64() - t0 < 3000000L)
			;
		regs("after ");
		for (i = 0; i < 2000; i++)
			if (bs[i] != ((((500UL * 20 - 3) << 1)) |
				      (unsigned long)((i & 1) ^ 1)))
				bad++;
		cnt(GPIOC_CNT_READ, CNT_PIN, 0, &v);
		printf("run5: sleep mid-stream -> counted %lld, busy=%d, "
		       "corrupted words %d\n", v, pc3_pioout_busy(), bad);
		if (pc3_pioout_busy()) {
			PC3_REG(PC3_DMA + 0x464) = 1UL << PIOOUT_DMA_CH;
			pc3_pioout_stop();
		}
	}

	/* run6: pioouttest T2's EXACT shape - FIN active on the counter
	   (which runs countpin's 1ms gate timer) while the stream plays
	   and the process sleeps.  run7: the size axis - 8000 words,
	   CIN.  Whichever sticks is the culprit. */
	{
		long long v = 0, t0;
		int i;

		cnt(GPIOC_CNT_FIN, CNT_PIN, 500, NULL);
		pc3_pioout_setup(PIOOUT_ORG_BS, 3, SIG_PIN, 0, 0);
		for (i = 0; i < 4000; i++)
			bs[i] = ((250UL * 20 - 3) << 1) | ((i & 1) ^ 1);
		pc3_pioout_start(bs, 4000);	/* 1s */
		sleep(1);
		regs("r6 mid");
		cnt(GPIOC_CNT_READ, CNT_PIN, 0, &v);
		printf("  r6 FIN latch: %lld\n", v);
		t0 = pc3_us64();
		while (pc3_pioout_busy() && pc3_us64() - t0 < 3000000L)
			;
		printf("run6: FIN+sleep -> latch %lld, busy=%d\n",
		       v, pc3_pioout_busy());
		if (pc3_pioout_busy()) {
			PC3_REG(PC3_DMA + 0x464) = 1UL << PIOOUT_DMA_CH;
			pc3_pioout_stop();
		}
	}
	{
		long long v = 0, t0;
		int i;

		pc3_pioout_setup(PIOOUT_ORG_BS, 3, SIG_PIN, 0, 0);
		cnt(GPIOC_CNT_CIN, CNT_PIN, 3, NULL);
		for (i = 0; i < 8000; i++)
			bs[i] = ((250UL * 20 - 3) << 1) | ((i & 1) ^ 1);
		pc3_pioout_start(bs, 8000);	/* 2s */
		sleep(1);
		regs("r7 mid");
		t0 = pc3_us64();
		while (pc3_pioout_busy() && pc3_us64() - t0 < 4000000L)
			;
		v = 0;
		cnt(GPIOC_CNT_READ, CNT_PIN, 0, &v);
		printf("run7: 8000+sleep -> counted %lld, busy=%d\n",
		       v, pc3_pioout_busy());
		if (pc3_pioout_busy()) {
			PC3_REG(PC3_DMA + 0x464) = 1UL << PIOOUT_DMA_CH;
			pc3_pioout_stop();
		}
	}
	return 0;
}

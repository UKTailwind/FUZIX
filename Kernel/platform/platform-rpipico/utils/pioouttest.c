/*
 *	Exercise the fixed PIO output programs (kernel: pioout.c,
 *	design: PLAN-pioout.md) from C, before BASIC gets them.
 *
 *	Rig: GP2 wired to GP4, and a real WS2812 strip (12 LEDs) on GP7.
 *	The counting inputs are the instrument for the electrical half -
 *	a bitstream of n edges must count EXACTLY n on the wire, because
 *	the PIO and the counter never miss - and the strip is the visual
 *	half: red, green, blue, white, off, one second apart, checked by
 *	the person at the bench.
 *
 *	Everything here runs with interrupts ON and the machine alive -
 *	the entire point of the design.  The old bit-bang route would
 *	have masked interrupts for every element of every test below.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/pc3io.h>

#define SIG_PIN		2	/* bitstream out, wired to CNT_PIN */
#define CNT_PIN		4
#define STRIP_PIN	7	/* the real 12-LED strip */
#define NLEDS		12

static int gfd;
static int fails;

/*	The words are BUILT IN THE KERNEL'S PSRAM BUFFER, not in a local
 *	array, and that is the whole lesson of the first version of this
 *	test: this kernel's swapper rearranges process memory in 4K
 *	chunks on every context switch, so a DMA reading a process array
 *	got other processes' bytes whenever this program slept - and a
 *	garbage duration word parks the state machine for most of a
 *	minute.  GPIOC_PIOOUT_BUF says where the safe buffer is. */
static unsigned long *bs;

static void check(const char *what, int ok)
{
	printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		fails++;
}

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

/*	n edges, us apart, starting from a driven low - element i drives
 *	level 1,0,1,0..., so every element is exactly one edge on the
 *	wire and the wire ends low when n is even. */
static void mkedges(int n, long us)
{
	unsigned long cyc = (unsigned long)us * 20 - 3;
	int i;

	for (i = 0; i < n; i++)
		bs[i] = (cyc << 1) | ((i & 1) ^ 1);
}

static int emit_wait(int org, const unsigned long *w, int n, long limit_us)
{
	long long t0 = pc3_us64();

	pc3_pioout_start(w, (unsigned long)n);
	while (pc3_pioout_busy())
		if (pc3_us64() - t0 > limit_us) {
			pc3_pioout_stop();
			return -1;
		}
	return 0;
}

static void claim_all(void)
{
	if (pc3_claim(PLK_PIN, SIG_PIN) || pc3_claim(PLK_PIN, CNT_PIN) ||
	    pc3_claim(PLK_PIO, PIOOUT_PLK_IDX) ||
	    pc3_claim(PLK_DMA, PIOOUT_DMA_CH)) {
		perror("claim");
		exit(1);
	}
}

static void release_all(void)
{
	pc3_release(PLK_DMA, PIOOUT_DMA_CH);
	pc3_release(PLK_PIO, PIOOUT_PLK_IDX);
	pc3_release(PLK_PIN, SIG_PIN);
	pc3_release(PLK_PIN, CNT_PIN);
}

int main(void)
{
	long long v;
	long long t0;
	pid_t pid;
	int st, r;

	gfd = open("/dev/gpio", O_RDWR);
	if (gfd < 0) {
		perror("/dev/gpio");
		return 1;
	}
	{
		struct pioout_buf pb;
		if (ioctl(gfd, GPIOC_PIOOUT_BUF, &pb) < 0) {
			perror("pioout buf");
			return 1;
		}
		bs = (unsigned long *)pb.addr;
	}
	claim_all();

	/* T1: a bitstream of exactly 2000 edges, 250us apart, counted on
	   the wire.  Setup parks the machine BEFORE counting starts, so
	   the count sees only the stream. */
	pc3_pioout_pin(SIG_PIN);
	pc3_pioout_setup(PIOOUT_ORG_BS, 3, SIG_PIN, 0, 0);
	check("CIN config", cnt(GPIOC_CNT_CIN, CNT_PIN, 3, NULL) == 0);
	mkedges(2000, 250);
	check("bitstream emits", emit_wait(PIOOUT_ORG_BS, bs, 2000,
					   2000000L) == 0);
	cnt(GPIOC_CNT_READ, CNT_PIN, 0, &v);
	printf("  2000 edges counted: %lld\n", v);
	check("edge count exact", v == 2000);

	/* T2: frequency truth, WITH A SLEEP mid-stream - the shape that
	   found the swapper.  8000 edges at 250us: rising every 500us =
	   2000/s, so the 500ms gate latches 1000.  The gate's phase
	   starts at CONFIG, so configure AFTER the stream is running -
	   the several-ms PSRAM fill would otherwise eat the head of the
	   first gate (it read 997 when configured first). */
	pc3_pioout_setup(PIOOUT_ORG_BS, 3, SIG_PIN, 0, 0);
	mkedges(8000, 250);
	pc3_pioout_start(bs, 8000);
	check("FIN config", cnt(GPIOC_CNT_FIN, CNT_PIN, 500, NULL) == 0);
	sleep(1);
	cnt(GPIOC_CNT_READ, CNT_PIN, 0, &v);
	printf("  FIN latch mid-stream: %lld\n", v);
	check("FIN 2kHz from stream", v >= 999 && v <= 1001);
	t0 = pc3_us64();
	while (pc3_pioout_busy())
		if (pc3_us64() - t0 > 3000000L)
			break;
	check("long stream completes", !pc3_pioout_busy());

	/* T3: period truth.  Edges 1000us apart -> rising every 2ms;
	   PER over 100 cycles latches 200ms. */
	check("PER config", cnt(GPIOC_CNT_PER, CNT_PIN, 100, NULL) == 0);
	pc3_pioout_setup(PIOOUT_ORG_BS, 3, SIG_PIN, 0, 0);
	mkedges(1000, 1000);
	pc3_pioout_start(bs, 1000);
	sleep(1);
	cnt(GPIOC_CNT_READ, CNT_PIN, 0, &v);
	printf("  PER 100-cycle latch: %lldms\n", v);
	check("PER 2ms period", v >= 199 && v <= 201);
	t0 = pc3_us64();
	while (pc3_pioout_busy())
		if (pc3_us64() - t0 > 1000000L)
			break;

	/* T4: the open-collector variant, edges through the pull-up.
	   dir words: 1 = drive low, 0 = release high; even count ends
	   released, MMBasic's rule.  200 edges at 250us. */
	{
		unsigned long cyc = 250UL * 20 - 3;
		int i;
		for (i = 0; i < 200; i++)
			bs[i] = (cyc << 1) | ((i & 1) ^ 1);
	}
	PC3_REG(PC3_PAD(SIG_PIN) + PC3_SET) = PC3_PAD_PUE;
	cnt(GPIOC_CNT_CIN, CNT_PIN, 3, NULL);
	pc3_pioout_setup(PIOOUT_ORG_BSOC, 3, SIG_PIN, 0, 0);
	check("OC emits", emit_wait(PIOOUT_ORG_BSOC, bs, 200, 500000L) == 0);
	cnt(GPIOC_CNT_READ, CNT_PIN, 0, &v);
	printf("  OC edges counted: %lld\n", v);
	check("OC edge count", v >= 199 && v <= 201);
	PC3_REG(PC3_PAD(SIG_PIN) + PC3_CLR) = PC3_PAD_PUE;

	/* T5: a WS2812 frame's exact bit count, electrically.  12 LEDs
	   of zeros = 288 bits = one high pulse each = 576 edges.  Setup
	   BEFORE the counter: parking the machine drives the pin to its
	   start level, and that settle must not be counted. */
	pc3_pioout_setup(PIOOUT_ORG_WSB, 4, SIG_PIN, 24, 0);
	cnt(GPIOC_CNT_CIN, CNT_PIN, 3, NULL);
	memset(bs, 0, NLEDS * 4);
	check("WS2812 frame emits", emit_wait(PIOOUT_ORG_WSB, bs, NLEDS,
					      500000L) == 0);
	cnt(GPIOC_CNT_READ, CNT_PIN, 0, &v);
	printf("  WS2812 zero-frame edges: %lld (expect 576)\n", v);
	check("WS2812 bit count", v == 2 * 24 * NLEDS);
	cnt(GPIOC_CNT_OFF, CNT_PIN, 0, NULL);

	/* T6: the REAL strip on GP7 - red, green, blue, white, off, a
	   second apart.  The person at the bench is the instrument.
	   Word = (G<<24)|(R<<16)|(B<<8), MMBasic's own wire order. */
	check("claim strip pin", pc3_claim(PLK_PIN, STRIP_PIN) == 0);
	pc3_pioout_pin(STRIP_PIN);
	{
		static const unsigned long shades[5] = {
			0x00FF0000UL,	/* red   */
			0xFF000000UL,	/* green */
			0x0000FF00UL,	/* blue  */
			0xFFFFFF00UL,	/* white */
			0x00000000UL	/* off   */
		};
		int s, i;
		for (s = 0; s < 5; s++) {
			for (i = 0; i < NLEDS; i++)
				bs[i] = shades[s];
			pc3_pioout_setup(PIOOUT_ORG_WSB, 4, STRIP_PIN,
					 24, 0);
			r = emit_wait(PIOOUT_ORG_WSB, bs, NLEDS, 500000L);
			if (r)
				break;
			sleep(1);
		}
		check("strip frames sent", r == 0);
		printf("  (watch the strip: red green blue white off)\n");
	}
	pc3_release(PLK_PIN, STRIP_PIN);

	/* T7: SIGKILL mid-stream.  The child dies with two seconds of
	   DMA still queued; the sweep must abort the channel, stop the
	   machine, and hand everything to the next claimant clean. */
	release_all();
	pid = fork();
	if (pid == 0) {
		claim_all();
		pc3_pioout_pin(SIG_PIN);
		pc3_pioout_setup(PIOOUT_ORG_BS, 3, SIG_PIN, 0, 0);
		mkedges(8000, 250);
		pc3_pioout_start(bs, 8000);
		sleep(30);	/* SIGKILL lands here */
		_exit(0);
	}
	sleep(1);
	kill(pid, SIGKILL);
	waitpid(pid, &st, 0);
	check("reclaim after SIGKILL", pc3_claim(PLK_PIN, SIG_PIN) == 0 &&
	      pc3_claim(PLK_PIN, CNT_PIN) == 0 &&
	      pc3_claim(PLK_PIO, PIOOUT_PLK_IDX) == 0 &&
	      pc3_claim(PLK_DMA, PIOOUT_DMA_CH) == 0);
	pc3_pioout_pin(SIG_PIN);
	pc3_pioout_setup(PIOOUT_ORG_BS, 3, SIG_PIN, 0, 0);
	cnt(GPIOC_CNT_CIN, CNT_PIN, 3, NULL);
	mkedges(2000, 250);
	check("emit after SIGKILL", emit_wait(PIOOUT_ORG_BS, bs, 2000,
					      2000000L) == 0);
	cnt(GPIOC_CNT_READ, CNT_PIN, 0, &v);
	printf("  post-kill edges: %lld\n", v);
	check("count after SIGKILL", v == 2000);
	cnt(GPIOC_CNT_OFF, CNT_PIN, 0, NULL);

	printf(fails ? "pioouttest: %d FAILED\n" : "pioouttest: all passed\n",
	       fails);
	return fails != 0;
}

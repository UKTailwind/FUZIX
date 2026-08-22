/*
 *	Exercise the counting inputs (kernel: countpin.c) from C, before
 *	BASIC gets them.  PLAN-count.md stage 1.
 *
 *	Rig: GP2 wired to GP4 on the test board.  GP2 is driven by its
 *	own PWM slice through <sys/pc3io.h>'s register wrappers - the
 *	same signal source the BASIC acceptance uses - so every figure
 *	below is measured against a frequency this program set itself.
 *
 *	PWM and the kernel's 1ms gate timer are both crystal-derived
 *	(clk_sys PLL and the 1MHz timer), so FIN at 1kHz over a 1s gate
 *	must answer 1000 give or take the one-count gate quantization -
 *	a tolerance is a bug, not a margin, and the assertions below are
 *	tight on purpose.
 *
 *	The part that matters most is T9, the locktest.c lesson applied
 *	here: a child killed with SIGKILL while counting must leave GP4
 *	claimable, and a fresh SETPIN must count afresh.  pinlock's
 *	death-sweep calls countpin_reset() - this is the test that proves
 *	that line runs.
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

#define SIG_PIN		2	/* PWM out */
#define CNT_PIN		4	/* wired to SIG_PIN */

static int gfd;
static int fails;

static void check(const char *what, int ok)
{
	printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok)
		fails++;
}

/*	375MHz clk_sys / 250 = 1.5MHz count clock; top picks the output
 *	frequency, level 50%.  16-bit top bounds this at >=23Hz, which is
 *	all this test needs. */
static void pwm_hz(long hz)
{
	int slice = pc3_pwm_slice(SIG_PIN);
	int chan = pc3_pwm_chan(SIG_PIN);
	unsigned long top = 1500000UL / hz - 1;

	pc3_pwm_enable(slice, 0);
	pc3_pwm_config(slice, 250, top, 0, 0, 0);
	pc3_pwm_level(slice, chan, (top + 1) / 2);
	pc3_pwm_enable(slice, 1);
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

/*	Count for n seconds and give back edges per second, measured
 *	against the microsecond clock rather than sleep()'s word. */
static long rate(int secs)
{
	long long v0 = 0, v1 = 0, t0, t1;

	cnt(GPIOC_CNT_SET, CNT_PIN, 0, &v0);
	v0 = 0;
	t0 = pc3_us64();
	sleep(secs);
	cnt(GPIOC_CNT_READ, CNT_PIN, 0, &v1);
	t1 = pc3_us64();
	return (long)((v1 * 1000000LL) / (t1 - t0));
}

int main(void)
{
	long long v;
	long r;
	pid_t pid;
	int st;

	gfd = open("/dev/gpio", O_RDWR);
	if (gfd < 0) {
		perror("/dev/gpio");
		return 1;
	}

	/* The signal source */
	if (pc3_claim(PLK_PIN, SIG_PIN) ||
	    pc3_claim(PLK_PWM, pc3_pwm_slice(SIG_PIN))) {
		perror("claim GP2/PWM");
		return 1;
	}
	pc3_pwm_pin(SIG_PIN);
	pwm_hz(1000);

	check("claim GP4", pc3_claim(PLK_PIN, CNT_PIN) == 0);

	/* T1: CIN counts the wire */
	check("CIN config", cnt(GPIOC_CNT_CIN, CNT_PIN, 1, NULL) == 0);
	r = rate(2);
	printf("  CIN rate %ld/s at 1kHz\n", r);
	check("CIN 1kHz", r >= 995 && r <= 1005);

	/* T2: Pin(n)=v stores ANY value, and reads back live */
	v = 12345;
	check("CIN set", cnt(GPIOC_CNT_SET, CNT_PIN, 0, &v) == 0);
	cnt(GPIOC_CNT_READ, CNT_PIN, 0, &v);
	printf("  after set 12345: %lld\n", v);
	check("CIN set/read", v >= 12345 && v < 12395);

	/* T3: both edges double the rate */
	check("CIN both config", cnt(GPIOC_CNT_CIN, CNT_PIN, 3, NULL) == 0);
	r = rate(2);
	printf("  CIN both-edges rate %ld/s\n", r);
	check("CIN both edges", r >= 1990 && r <= 2010);

	/* T4: FIN, the default 1s gate.  Sleep long enough that at least
	   one whole gate has completed since config. */
	check("FIN config", cnt(GPIOC_CNT_FIN, CNT_PIN, 1000, NULL) == 0);
	sleep(3);
	cnt(GPIOC_CNT_READ, CNT_PIN, 0, &v);
	printf("  FIN gate 1000: %lld\n", v);
	check("FIN 1kHz", v >= 999 && v <= 1001);

	/* T5: a 100ms gate scales the latch, not the answer */
	check("FIN 100 config", cnt(GPIOC_CNT_FIN, CNT_PIN, 100, NULL) == 0);
	sleep(1);
	cnt(GPIOC_CNT_READ, CNT_PIN, 0, &v);
	printf("  FIN gate 100: %lld\n", v);
	check("FIN 100ms gate", v >= 99 && v <= 101);

	/* T6: PER.  At 100Hz a cycle is 10ms; averaged over 50 the latch
	   is 500ms of count.  1kHz's 1ms period is too close to the
	   quantum to assert tightly, so the period tests run at 100Hz. */
	pwm_hz(100);
	check("PER config", cnt(GPIOC_CNT_PER, CNT_PIN, 1, NULL) == 0);
	sleep(1);
	cnt(GPIOC_CNT_READ, CNT_PIN, 0, &v);
	printf("  PER 1 cycle at 100Hz: %lldms\n", v);
	check("PER 100Hz", v >= 9 && v <= 11);
	check("PER 50 config", cnt(GPIOC_CNT_PER, CNT_PIN, 50, NULL) == 0);
	sleep(2);
	cnt(GPIOC_CNT_READ, CNT_PIN, 0, &v);
	printf("  PER 50 cycles at 100Hz: %lldms\n", v);
	check("PER averaged", v >= 495 && v <= 505);
	pwm_hz(1000);

	/* T7: refusals.  Wrong pin, no claim, bad option, SET on a
	   non-CIN mode. */
	check("pin 8 refused", cnt(GPIOC_CNT_CIN, 8, 1, NULL) < 0 &&
	      errno == EINVAL);
	check("unclaimed GP5 refused", cnt(GPIOC_CNT_CIN, 5, 1, NULL) < 0 &&
	      errno == EPERM);
	check("option 11 refused", cnt(GPIOC_CNT_CIN, CNT_PIN, 11, NULL) < 0 &&
	      errno == EINVAL);
	cnt(GPIOC_CNT_PER, CNT_PIN, 1, NULL);
	v = 0;
	check("SET on PER refused",
	      cnt(GPIOC_CNT_SET, CNT_PIN, 0, &v) < 0 && errno == EINVAL);

	/* T8: OFF returns the pin to ordinary duty */
	check("OFF", cnt(GPIOC_CNT_OFF, CNT_PIN, 0, NULL) == 0);
	check("READ after OFF refused",
	      cnt(GPIOC_CNT_READ, CNT_PIN, 0, &v) < 0 && errno == EINVAL);

	/* T9: THE death sweep.  A child killed mid-count must leave the
	   pin claimable and the counter clean for the next owner. */
	pc3_release(PLK_PIN, CNT_PIN);
	pid = fork();
	if (pid == 0) {
		if (pc3_claim(PLK_PIN, CNT_PIN))
			_exit(1);
		if (cnt(GPIOC_CNT_CIN, CNT_PIN, 1, NULL))
			_exit(1);
		sleep(30);	/* SIGKILL lands here */
		_exit(0);
	}
	sleep(1);		/* let it claim and start counting */
	kill(pid, SIGKILL);
	waitpid(pid, &st, 0);
	check("reclaim after SIGKILL", pc3_claim(PLK_PIN, CNT_PIN) == 0);
	check("recount after SIGKILL", cnt(GPIOC_CNT_CIN, CNT_PIN, 1, NULL) == 0);
	r = rate(2);
	printf("  post-kill CIN rate %ld/s\n", r);
	check("counts after SIGKILL", r >= 995 && r <= 1005);

	/* T10: the claim is enforced per PROCESS, not per fd */
	pid = fork();
	if (pid == 0) {
		long long cv = 0;
		_exit(cnt(GPIOC_CNT_READ, CNT_PIN, 0, &cv) < 0 &&
		      errno == EPERM ? 0 : 1);
	}
	waitpid(pid, &st, 0);
	check("other process refused", WIFEXITED(st) && WEXITSTATUS(st) == 0);

	cnt(GPIOC_CNT_OFF, CNT_PIN, 0, NULL);
	printf(fails ? "cnttest: %d FAILED\n" : "cnttest: all passed\n",
	       fails);
	return fails != 0;
}

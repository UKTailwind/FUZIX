/*
 *	Exercise the I/O header's ownership registry (kernel: pinlock.c).
 *
 *	Two things are being tested and only one of them is bookkeeping:
 *
 *	1. The policy and the contention - what may be claimed, what may
 *	   not, and that a second claimant is refused rather than quietly
 *	   sharing.
 *	2. THE RECLAIM, which is the part that matters.  A child claims
 *	   GP5, drives it high, and is then SIGKILLed - the most abnormal
 *	   death available, and the one a program cannot clean up after.
 *	   The pin must come back an input.
 *
 *	Test 2 reads SIO directly to see the pin's real state, because
 *	nothing else can tell the difference: /dev/gpio's read goes through
 *	the pad input buffer, which a reset turns OFF, so a released pin
 *	and a pin still driving high both read back 0 through the kernel.
 *	Reading the output-enable register is the only honest answer.
 *
 *	That the test can do this at all is the whole premise of the
 *	design: there is no MMU and the kernel never drops privilege, so a
 *	program reaches SIO with a load.  The lock says who SHOULD.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/gpio.h>
#include <sys/wait.h>
#include "../pico_ioctl.h"

/* RP2350 SIO (addressmap.h: 0xd0000000).  The low and high banks
   INTERLEAVE here, which is not the RP2040 layout: GPIO_OE is 0x020 on
   an RP2040 and 0x030 on this part, because gpio_hi_out and friends sit
   between.  GP0-31 are the low bank, GP32-47 the high. */
#define SIO_BASE	0xd0000000UL
#define SIO_GPIO_OUT	(*(volatile unsigned long *)(SIO_BASE + 0x010))
#define SIO_GPIO_OUT_SET (*(volatile unsigned long *)(SIO_BASE + 0x018))
#define SIO_GPIO_OE	(*(volatile unsigned long *)(SIO_BASE + 0x030))
#define SIO_GPIO_OE_SET	(*(volatile unsigned long *)(SIO_BASE + 0x038))

/* IO_BANK0 GPIOn_CTRL: two registers per pin, CTRL is the second.
   FUNCSEL 5 is SIO. */
#define IO_BANK0_BASE	0x40028000UL
#define GPIO_CTRL(n)	(*(volatile unsigned long *)(IO_BANK0_BASE + 8UL*(n) + 4))

#define TESTPIN		5		/* on the header, low bank */

static int fd, gfd;
static int fails;

static void ok(const char *what, int cond)
{
	printf("%-46s %s\n", what, cond ? "ok" : "FAIL");
	if (!cond)
		fails++;
}

static int plk(int req, int cls, int idx)
{
	struct pinlock_req rq;

	rq.cls = (unsigned char)cls;
	rq.idx = (unsigned char)idx;
	rq.flags = 0;
	rq.pad = 0;
	return ioctl(fd, req, &rq);
}

/* -1 and this errno? */
static int failed_with(int r, int e)
{
	return r == -1 && errno == e;
}

/* What the child does: take the pin, drive it hard, tell the parent, and
   then sit there until it is killed.  No cleanup path at all - that is
   the point. */
static void child(int pipefd)
{
	char c = 'x';

	if (plk(PLKIOC_CLAIM, PLK_PIN, TESTPIN)) {
		write(pipefd, "!", 1);
		_exit(1);
	}
	GPIO_CTRL(TESTPIN) = 5;			/* FUNCSEL = SIO */
	SIO_GPIO_OUT_SET = 1UL << TESTPIN;	/* high... */
	SIO_GPIO_OE_SET = 1UL << TESTPIN;	/* ...and driven */
	write(pipefd, &c, 1);
	for (;;)
		pause();
}

int main(void)
{
	int p[2], st, r;
	pid_t pid;
	char c;
	unsigned n, got;

	fd = open("/dev/sys", O_RDWR);
	if (fd < 0) {
		perror("/dev/sys");
		return 1;
	}
	gfd = open("/dev/gpio", O_RDWR);
	if (gfd < 0) {
		perror("/dev/gpio");
		return 1;
	}

	puts("-- policy --");
	ok("claim GP4 (header pin)", plk(PLKIOC_CLAIM, PLK_PIN, 4) == 0);
	ok("claim GP4 again is idempotent",
	   plk(PLKIOC_CLAIM, PLK_PIN, 4) == 0);
	ok("owner(GP4) is us",
	   plk(PLKIOC_OWNER, PLK_PIN, 4) == (int)getpid());
	ok("claim GP26 (the lone header pin)",
	   plk(PLKIOC_CLAIM, PLK_PIN, 26) == 0);
	ok("claim GP8 refused - not on the header",
	   failed_with(plk(PLKIOC_CLAIM, PLK_PIN, 8), EINVAL));
	ok("claim GP16 refused - display's",
	   failed_with(plk(PLKIOC_CLAIM, PLK_PIN, 16), EINVAL));
	ok("claim GP99 refused - not a pin",
	   failed_with(plk(PLKIOC_CLAIM, PLK_PIN, 99), EINVAL));
	ok("claim I2C0 refused - /dev/i2c and the RTC own it",
	   failed_with(plk(PLKIOC_CLAIM, PLK_I2C, 0), EINVAL));
	ok("claim I2C1 allowed", plk(PLKIOC_CLAIM, PLK_I2C, 1) == 0);
	ok("claim SPI1 refused - the SD card",
	   failed_with(plk(PLKIOC_CLAIM, PLK_SPI, 1), EINVAL));
	ok("claim SPI0 allowed", plk(PLKIOC_CLAIM, PLK_SPI, 0) == 0);
	ok("claim PWM slice 3 allowed", plk(PLKIOC_CLAIM, PLK_PWM, 3) == 0);
	ok("claim PIO0 refused for now",
	   failed_with(plk(PLKIOC_CLAIM, PLK_PIO, 0), EINVAL));

	puts("-- release --");
	ok("release GP4", plk(PLKIOC_RELEASE, PLK_PIN, 4) == 0);
	ok("owner(GP4) now free", plk(PLKIOC_OWNER, PLK_PIN, 4) == 0);
	ok("release GP4 twice refused",
	   failed_with(plk(PLKIOC_RELEASE, PLK_PIN, 4), EINVAL));
	ok("release something we never had",
	   failed_with(plk(PLKIOC_RELEASE, PLK_PIN, 6), EINVAL));
	plk(PLKIOC_RELEASE, PLK_PIN, 26);
	plk(PLKIOC_RELEASE, PLK_I2C, 1);
	plk(PLKIOC_RELEASE, PLK_SPI, 0);
	plk(PLKIOC_RELEASE, PLK_PWM, 3);

	puts("-- contention and reclaim --");
	if (pipe(p)) {
		perror("pipe");
		return 1;
	}
	pid = fork();
	if (pid == 0) {
		close(p[0]);
		child(p[1]);
	}
	if (pid < 0) {
		perror("fork");
		return 1;
	}
	close(p[1]);
	c = 0;
	if (read(p[0], &c, 1) != 1 || c != 'x') {
		puts("child did not get the pin - giving up");
		kill(pid, SIGKILL);
		return 1;
	}

	/* The child is alive and holding it. */
	ok("owner(GP5) is the child",
	   plk(PLKIOC_OWNER, PLK_PIN, TESTPIN) == (int)pid);
	ok("our claim on GP5 is refused",
	   failed_with(plk(PLKIOC_CLAIM, PLK_PIN, TESTPIN), EBUSY));
	ok("GP5 is driving (proves the test is not vacuous)",
	   (SIO_GPIO_OE & (1UL << TESTPIN)) != 0);

	/* Kill it the way a program cannot recover from. */
	kill(pid, SIGKILL);
	while (wait(&st) != pid)
		;

	ok("GP5 output disabled after SIGKILL",
	   (SIO_GPIO_OE & (1UL << TESTPIN)) == 0);
	ok("owner(GP5) free after SIGKILL",
	   plk(PLKIOC_OWNER, PLK_PIN, TESTPIN) == 0);
	ok("GP5 claimable again",
	   plk(PLKIOC_CLAIM, PLK_PIN, TESTPIN) == 0);
	plk(PLKIOC_RELEASE, PLK_PIN, TESTPIN);

	puts("-- exhaustion --");
	/* 24 slots.  Run it in a child so the table is cleaned up by the
	   very mechanism under test rather than by careful bookkeeping
	   here - and so a bug leaves the next run a clean machine. */
	pid = fork();
	if (pid == 0) {
		static const unsigned char hdr[] = {
			0, 1, 2, 3, 4, 5, 6, 7, 26,
			34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46
		};
		got = 0;
		for (n = 0; n < sizeof(hdr); n++)
			if (plk(PLKIOC_CLAIM, PLK_PIN, hdr[n]) == 0)
				got++;
		/* 22 pins + 2 blocks fills it exactly. */
		if (plk(PLKIOC_CLAIM, PLK_I2C, 1) == 0)
			got++;
		if (plk(PLKIOC_CLAIM, PLK_SPI, 0) == 0)
			got++;
		r = plk(PLKIOC_CLAIM, PLK_PWM, 0);
		_exit(got == 24 && failed_with(r, ENOMEM) ? 0 : 1);
	}
	while (wait(&st) != pid)
		;
	ok("24 claims fit, the 25th is ENOMEM", WIFEXITED(st)
	   && WEXITSTATUS(st) == 0);
	ok("the whole header came back with the child",
	   plk(PLKIOC_OWNER, PLK_PIN, 0) == 0
	   && plk(PLKIOC_OWNER, PLK_PIN, 46) == 0
	   && plk(PLKIOC_OWNER, PLK_SPI, 0) == 0);

	printf("\n%s\n", fails ? "FAILURES" : "all passed");
	return fails ? 1 : 0;
}

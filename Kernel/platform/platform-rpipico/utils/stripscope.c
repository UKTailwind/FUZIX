/*
 *	A software oscilloscope for the WS2812 program: slow the state
 *	machine ~3600x by poking SM_CLKDIV, emit ONE LED's worth of a
 *	known pattern on GP2, and sample GP4 (wired to it) from the CPU,
 *	recording every transition.  At 5.7kHz SM clock the half-bits
 *	are milliseconds, so a polling loop measures them exactly - and
 *	the cycle counts are the same ones the 20MHz configuration uses,
 *	so a correct slow waveform proves the fast one.
 *
 *	Pattern 0xAA55A5 (G=10101010 R=01010101 B=10100101), variant B:
 *	  1-bit: high 16 cycles, low 7   (at div 65535: 2.796ms / 1.223ms)
 *	  0-bit: high 8,          low 15 (1.398ms / 2.621ms)
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

static unsigned long *bs;
static long long tr[80];	/* transition timestamps */
static int lv[80];

int main(void)
{
	long long t0, tend;
	int gfd, i, n = 0, last;

	gfd = open("/dev/gpio", O_RDWR);
	if (gfd < 0) {
		perror("/dev/gpio");
		return 1;
	}
	{
		struct pioout_buf pb;
		if (ioctl(gfd, GPIOC_PIOOUT_BUF, &pb) < 0) {
			perror("buf");
			return 1;
		}
		bs = (unsigned long *)pb.addr;
	}
	if (pc3_claim(PLK_PIN, SIG_PIN) || pc3_claim(PLK_PIN, CNT_PIN) ||
	    pc3_claim(PLK_PIO, PIOOUT_PLK_IDX) ||
	    pc3_claim(PLK_DMA, PIOOUT_DMA_CH)) {
		perror("claim");
		return 1;
	}
	pc3_pioout_pin(SIG_PIN);
	pc3_pin_in(CNT_PIN, 0);

	bs[0] = 0xAA55A500UL;
	pc3_pioout_setup(PIOOUT_ORG_WSB, 4, SIG_PIN, 24, 0);
	/* ~3600x slower: divider 65535 -> 5.722kHz, bit 4.02ms */
	PC3_REG(PC3_SM_CLKDIV) = 0xFFFF0000UL;
	PC3_REG(PC3_PIO1_CTRL + PC3_PIO_SET) =
		(1UL << (8 + PIOOUT_SM));	/* clkdiv restart */

	last = pc3_pin_get(CNT_PIN);
	pc3_pioout_start(bs, 1);
	t0 = pc3_us64();
	tend = t0 + 24LL * 4021 + 200000;	/* the frame + slack */
	while (pc3_us64() < tend && n < 80) {
		int v = pc3_pin_get(CNT_PIN);
		if (v != last) {
			tr[n] = pc3_us64();
			lv[n] = v;
			n++;
			last = v;
		}
	}
	PC3_REG(PC3_DMA + 0x464) = 1UL << PIOOUT_DMA_CH;
	pc3_pioout_stop();

	printf("%d transitions.  Pulse widths (us), level before each:\n", n);
	for (i = 1; i < n; i++)
		printf("  %s %ld\n", lv[i - 1] ? "HIGH" : "LOW ",
		       (long)(tr[i] - tr[i - 1]));
	printf("expect 1-bit HIGH 2796 / LOW 1223; 0-bit HIGH 1398 / LOW 2621\n");
	printf("(trailing LOW is the inter-frame idle)\n");
	return 0;
}

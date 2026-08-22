/*
 *	The WS2812 strip, and nothing else: alternate red and green
 *	frames on the given pin (default GP7) once a second, printing
 *	the pin's mux and pad registers and the machine's state around
 *	the first frame - so "nothing on the LEDs" can be split into
 *	"the pin is not driving" (registers say so) versus "the pin is
 *	driving and the strip disagrees" (wiring, power, or logic level:
 *	a 5V-supplied strip wants ~3.5V data and GP7 gives 3.3V).
 *
 *	./striptest [pin] [variant] [divmult]
 *	  variant: B (default), O, S - MMBasic's three timing sets
 *	  divmult: SM clock divider multiplier - 1 (default) is the
 *	  800kHz-class rate, 2 halves it for 400kHz-class chips
 *	  (WS2811 and friends)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/pc3io.h>

#define NLEDS	12

static unsigned long *bs;

int main(int argc, char *argv[])
{
	int pin = argc > 1 ? atoi(argv[1]) : 7;
	int org = PIOOUT_ORG_WSB;
	long mult = argc > 3 ? atol(argv[3]) : 1;
	long long t0;
	int gfd, i, f;

	if (argc > 2) {
		if (argv[2][0] == 'O' || argv[2][0] == 'o')
			org = PIOOUT_ORG_WSO;
		else if (argv[2][0] == 'S' || argv[2][0] == 's')
			org = PIOOUT_ORG_WSS;
	}
	if (mult < 1 || mult > 100)
		mult = 1;

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
	if (pc3_claim(PLK_PIN, pin) || pc3_claim(PLK_PIO, PIOOUT_PLK_IDX) ||
	    pc3_claim(PLK_DMA, PIOOUT_DMA_CH)) {
		perror("claim");
		return 1;
	}
	pc3_pioout_pin(pin);
	pc3_pioout_setup(org, 4, pin, 24, 0);

	printf("gp%d ctrl %08lx (want funcsel 7)  pad %08lx (want IE=40, no OD/ISO)\n",
	       pin, PC3_REG(PC3_GPIO_CTRL(pin)), PC3_REG(PC3_PAD(pin)));
	printf("pinctl %08lx exec %08lx shift %08lx clkdiv %08lx\n",
	       PC3_REG(PC3_SM_PINCTRL), PC3_REG(PC3_SM_EXECCTRL),
	       PC3_REG(PC3_SM_SHIFTCTRL), PC3_REG(PC3_SM_CLKDIV));

	for (f = 0; f < 10; f++) {
		for (i = 0; i < NLEDS; i++)
			bs[i] = (f & 1) ? 0xFF000000UL	/* green */
					: 0x00FF0000UL;	/* red   */
		pc3_pioout_setup(org, 4, pin, 24, 0);
		if (mult > 1) {
			/* base divider is 4992/256ths (18.75); scale it */
			unsigned long d = 4992UL * (unsigned long)mult;
			PC3_REG(PC3_SM_CLKDIV) =
				((d >> 8) << 16) | ((d & 0xFF) << 8);
			PC3_REG(PC3_PIO1_CTRL + PC3_PIO_SET) =
				1UL << (8 + PIOOUT_SM);
		}
		pc3_pioout_start(bs, NLEDS);
		t0 = pc3_us64();
		while (pc3_pioout_busy() && pc3_us64() - t0 < 500000L)
			;
		if (f == 0)
			printf("frame 1: busy=%d fdebug %08lx dmacnt %lu\n",
			       pc3_pioout_busy(), PC3_REG(PC3_PIO1_FDEBUG),
			       PC3_REG(PC3_DMA_COUNT));
		sleep(1);
	}
	printf("gp%d after: ctrl %08lx pad %08lx\n",
	       pin, PC3_REG(PC3_GPIO_CTRL(pin)), PC3_REG(PC3_PAD(pin)));
	printf("done - the strip should have blinked red/green 10 times\n");
	return 0;
}

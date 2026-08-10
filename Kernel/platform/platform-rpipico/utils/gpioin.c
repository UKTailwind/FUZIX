/*
 * gpioin <outpin> <inpin> - loopback probe.
 *
 *   gpioin 33 35
 *
 * Prints the RAW return of every ioctl and errno with it, because the
 * BASIC path maps a failure onto 0 and a 0 read looks exactly like a
 * pin that is working and low.  Written after a GP33->GP35 loopback
 * read 0 in both directions and three plausible explanations in a row
 * turned out to be wrong.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>

#include <sys/gpio.h>
#include <sys/ioctl.h>
#include <sys/pc3io.h>

static int fd;

/*
 * The joystick read - GP34-GP37, pulled up and active low, read the way
 * everything reads pins now: claim them, then the registers.  bit1 is
 * GP35.
 *
 * Running it beside GPIOC_GETBYTE on the same pin is what separates
 * "my read is wrong" from "the output is not driving".
 *
 * This used to go through PICOIOC_ADVAL, and must not go back: those
 * selectors are retired and answer 0, which in a diagnostic whose whole
 * point is that "0" is ambiguous would be the worst possible bug.
 */
static void joy(const char *what)
{
	int i, v = 0, refused = 0;

	for (i = 34; i <= 37; i++) {
		if (pc3_claim(PLK_PIN, i)) {
			refused = i;
			break;
		}
		pc3_pin_in(i, 1);
		PC3_REG(PC3_PAD(i) + PC3_SET) = PC3_PAD_SCHMITT;
	}
	if (refused) {
		printf("%s -> cannot claim GP%d, errno %d\n", what, refused,
		       errno);
		return;
	}
	for (i = 0; i < 4; i++)
		if (!pc3_pin_get(34 + i))
			v |= 1 << i;
	printf("%s -> %d   (bit1 = GP35, 1 = LOW/pressed)\n", what, v);
}

static int req(int r, const char *what, uint8_t pin, uint8_t val)
{
	struct gpioreq gr;
	int n;

	gr.pin = pin;
	gr.val = val;
	errno = 0;
	n = ioctl(fd, r, &gr);
	printf("%-18s pin %2u val %u -> %d", what, pin, val, n);
	if (n < 0)
		printf("  errno %d", errno);
	printf("\n");
	return n;
}

int main(int argc, char **argv)
{
	uint8_t o, i;
	int n;

	if (argc != 3) {
		fprintf(stderr, "usage: %s <outpin> <inpin>\n", argv[0]);
		return 1;
	}
	o = (uint8_t)strtoul(argv[1], NULL, 10);
	i = (uint8_t)strtoul(argv[2], NULL, 10);

	fd = open("/dev/gpio", O_RDWR, 0);
	if (fd == -1) {
		perror("/dev/gpio");
		return 1;
	}

	errno = 0;
	n = ioctl(fd, GPIOC_COUNT, 0);
	printf("GPIOC_COUNT        -> %d", n);
	if (n < 0)
		printf("  errno %d", errno);
	printf("   (48 = the new driver, 28 = the old one)\n");

	req(GPIOC_SETRW, "SETRW out", o, 1);
	req(GPIOC_SETRW, "SETRW in", i, 0);

	req(GPIOC_SET, "SET high", o, 1);
	req(GPIOC_GETBYTE, "GETBYTE", i, 0);
	/* the driving pin read back too: if THAT is 0 the output is the
	   problem, not the input */
	req(GPIOC_GETBYTE, "GETBYTE self", o, 0);
	joy("  joystick nibble");

	req(GPIOC_SET, "SET low", o, 0);
	req(GPIOC_GETBYTE, "GETBYTE", i, 0);
	req(GPIOC_GETBYTE, "GETBYTE self", o, 0);
	joy("  joystick nibble");

	close(fd);
	return 0;
}

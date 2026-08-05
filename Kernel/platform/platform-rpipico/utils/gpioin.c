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

static int fd;

/*
 * The joystick read - GP34-37 through PICOIOC_ADVAL, which is the path
 * that is KNOWN to work on the high bank: it enables the input, pulls
 * up, and reads with gpio_get_all64().  bit1 is GP35.
 *
 * Running it beside GPIOC_GETBYTE on the same pin is what separates
 * "my read is wrong" from "the output is not driving".
 */
#define PICOIOC_ADVAL 0x0009

static void joy(const char *what)
{
	int sys, n = 0, r;

	sys = open("/dev/sys", O_RDWR, 0);
	if (sys < 0) {
		printf("%s: no /dev/sys\n", what);
		return;
	}
	errno = 0;
	r = ioctl(sys, PICOIOC_ADVAL, &n);
	printf("%s -> %d", what, r);
	if (r < 0)
		printf("  errno %d", errno);
	else
		printf("   (bit1 = GP35, 1 = LOW/pressed)");
	printf("\n");
	close(sys);
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

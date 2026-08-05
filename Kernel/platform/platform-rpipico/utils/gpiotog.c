/*
 * gpiotog - toggle a GPIO pin at a fixed rate and say what really
 * happened.
 *
 *   gpiotog <pin> <ms> <toggles>
 *
 * Two of these on two pins is the first test of whether this machine
 * can share a resource between processes at all: each owns one pin,
 * each sleeps between changes, and an LED on each says whether they
 * are independent.
 *
 * But an LED only says "it works".  This counts, and reports the
 * elapsed time against the time the sleeps asked for, because the
 * number is what matters later: an MP3 player has to keep a buffer fed
 * while something else runs, and the question is not whether the
 * scheduler works but how much it slips under load.
 *
 * Deliberately does NOT claim the pin.  Nothing here does yet - that
 * is the point of the experiment.  gpio_put() and gpio_set_dir() use
 * the RP2350's atomic set/clear registers, so two processes on two
 * pins cannot corrupt each other whatever the scheduler does; two
 * processes on ONE pin should produce nonsense but not damage.  Both
 * are worth seeing rather than assuming.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include <sys/gpio.h>
#include <sys/ioctl.h>

int main(int argc, char **argv)
{
	struct gpioreq gr;
	unsigned long ms, want, done = 0;
	time_t t0, t1;
	long secs, asked;
	int fd, val = 0;

	if (argc != 4) {
		fprintf(stderr, "usage: %s <pin> <ms> <toggles>\n", argv[0]);
		return 1;
	}
	gr.pin = (uint8_t)strtoul(argv[1], NULL, 10);
	ms = strtoul(argv[2], NULL, 10);
	want = strtoul(argv[3], NULL, 10);

	fd = open("/dev/gpio", O_RDWR, 0);
	if (fd == -1) {
		perror("/dev/gpio");
		return 1;
	}

	t0 = time(NULL);
	while (done < want) {
		gr.val = (uint8_t)val;
		if (ioctl(fd, GPIOC_SET, &gr) != 0) {
			perror("GPIOC_SET");
			close(fd);
			return 1;
		}
		val = !val;
		done++;
		if (ms)
			usleep(ms * 1000UL);
	}
	t1 = time(NULL);

	/* Leave it off rather than wherever the count happened to end. */
	gr.val = 0;
	ioctl(fd, GPIOC_SET, &gr);
	close(fd);

	secs = (long)(t1 - t0);
	asked = (long)((ms * want) / 1000UL);
	printf("pin %u: %lu toggles in %lds, sleeps asked for %lds\n",
	       (unsigned)gr.pin, done, secs, asked);
	return 0;
}

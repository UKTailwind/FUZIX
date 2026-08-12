/*
 * busytest - which kind of core0 load disturbs the display?
 *
 *   busytest -s [seconds]     hammer SRAM only  (process memory)
 *   busytest -p [seconds]     hammer PSRAM only (the arena, over QMI)
 *   busytest -c [seconds]     pure registers, almost no memory at all
 *
 * mp3bench made the display flecking catastrophic while never opening
 * the audio device, which ruled the audio path out and left "core0 is
 * busy" as the cause.  But mp3bench is three loads at once - it works
 * SRAM (its decode buffer), PSRAM (32K of decoder state in the arena)
 * and the FPU, all flat out - so on its own it does not say WHICH of
 * them core1 cannot tolerate.
 *
 * These three do exactly one thing each.  Whichever of them reproduces
 * the flecking is the one to design around; the ones that do not are
 * ruled out for good.  -c is the control: if even that flecks, the
 * problem is not memory bandwidth at all but something about core0
 * simply running.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>

#include "../pico_ioctl.h"

#define BUFLEN (32u * 1024)

static unsigned char sram[BUFLEN];		/* BSS: process memory */

static volatile unsigned long sink;

int main(int argc, char *argv[])
{
	struct psram_req rq;
	volatile unsigned long *p;
	unsigned long i, n, acc = 0;
	int fd, seconds = 20, mode = 's', a;
	clock_t end;

	for (a = 1; a < argc; a++) {
		if (argv[a][0] == '-')
			mode = argv[a][1];
		else
			seconds = atoi(argv[a]);
	}
	if (seconds <= 0)
		seconds = 20;

	if (mode == 'p') {
		fd = open("/dev/sys", O_RDWR);
		rq.len = BUFLEN;
		if (fd < 0 || ioctl(fd, PSRAMIOC_ALLOC, &rq) < 0 || !rq.base) {
			printf("no PSRAM arena\n");
			return 1;
		}
		close(fd);
		p = (volatile unsigned long *)rq.base;
		printf("hammering PSRAM at %lx for %d seconds\n",
		       (unsigned long)rq.base, seconds);
	} else {
		p = (volatile unsigned long *)sram;
		printf("hammering %s for %d seconds\n",
		       mode == 'c' ? "registers only" : "SRAM", seconds);
	}

	n = BUFLEN / sizeof(unsigned long);
	end = clock() + (clock_t)seconds * CLOCKS_PER_SEC;

	while (clock() < end) {
		if (mode == 'c') {
			/* No memory traffic worth the name: everything the
			 * compiler can keep in registers. */
			for (i = 0; i < 200000; i++)
				acc = acc * 1103515245UL + 12345UL;
		} else {
			for (i = 0; i < n; i++)
				acc += p[i];
			for (i = 0; i < n; i++)
				p[i] = acc + i;
		}
	}
	sink = acc;
	printf("done\n");
	return 0;
}

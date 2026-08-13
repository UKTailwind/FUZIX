/* snd - the BBC synth ioctls fired directly, no bbcbasic.
 *
 * The manual's Sound tests plus the discriminators this afternoon
 * needed.  Run bare for the full sequence or with a number for one
 * test.  Every run starts with SNDIOC_QUIET so no test inherits a
 * note a previous run left synced-and-waiting - which is exactly how
 * the "chord changing half way through" was manufactured: sync=1
 * releases notes in PAIRS, so a three-note &1xx chord plays two and
 * leaves one stuck for the next run to trigger.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "../pico_ioctl.h"

static int fd;

static void note(int chan, int amp, int pitch, int dur)
{
	struct snd_cmd c;

	c.chan = chan; c.amp = amp; c.pitch = pitch; c.dur = dur;
	if (ioctl(fd, SNDIOC_SOUND, &c) < 0)
		perror("SOUND");
}

int main(int argc, char *argv[])
{
	int which = argc > 1 ? atoi(argv[1]) : 0;

	fd = open("/dev/sys", O_RDWR);
	if (fd < 0) {
		perror("/dev/sys");
		return 1;
	}

	ioctl(fd, SNDIOC_QUIET, 0);

	if (!which || which == 1) {
		printf("1 tone: 2s 440 Hz, amp -7\n");
		note(1, -7, 89, 40);
		sleep(4);
	}
	if (!which || which == 2) {
		/* N,T,PI1-3,PN1-3,AA,AD,AS,AR,ALA,ALD - the manual's
		 * siren envelope at half amplitude */
		static unsigned char env[14] = {
			1, 1, 4, 0xFC, 4, 10, 10, 10,
			63, 0xFF, 0, 0xFB, 60, 50
		};
		printf("2 siren: 1.5s\n");
		if (ioctl(fd, SNDIOC_ENV, env) < 0)
			perror("ENV");
		note(1, 1, 100, 30);
		sleep(4);
	}
	if (!which || which == 3) {
		printf("3 chord, sync=2 (right for three notes): 1.5s\n");
		note(0x201, -7, 53, 30);
		note(0x202, -7, 69, 30);
		note(0x203, -7, 81, 30);
		sleep(4);
	}
	if (!which || which == 4) {
		printf("4 chord, no sync (control): 1.5s\n");
		note(1, -7, 53, 30);
		note(2, -7, 69, 30);
		note(3, -7, 81, 30);
		sleep(4);
	}
	if (!which || which == 5) {
		printf("5 warble probe: 220 then 440 then 880 Hz\n");
		note(1, -7, 41, 30);
		sleep(3);
		note(1, -7, 89, 30);
		sleep(3);
		note(1, -7, 137, 30);
		sleep(4);
	}
	printf("done\n");
	return 0;
}

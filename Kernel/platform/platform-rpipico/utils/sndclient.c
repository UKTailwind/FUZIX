/* sndclient - one PLAY SOUND record to the daemon's FIFO, then exit.
 *
 * The minimal client: no bcrun, no owner polling, no interrupt
 * machinery.  With a silent playsnd holding the stream, this leaves a
 * 440 Hz sine playing forever with NOBODY else touching the audio
 * ioctls - the discriminator for whether the queue collapse is
 * provoked by the polling a BASIC client does.
 *
 *   sndclient [freq_hz [vol]]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "mmb_playctl.h"

int main(int argc, char *argv[])
{
	struct mm_playmsg m;
	int fd, hz = 440, vol = 25;

	if (argc >= 2)
		hz = atoi(argv[1]);
	if (argc >= 3)
		vol = atoi(argv[2]);

	memset(&m, 0, sizeof(m));
	m.ver = MM_PLAYCTL_VER;
	m.op = MM_PLAY_SOUND;
	m.a = 1;			/* voice 1 */
	m.b = 3;			/* both sides */
	m.p1 = MM_SND_SINE;
	m.p2 = hz * 1000;		/* mHz */
	m.p3 = vol;

	fd = open(MM_PLAYCTL_FIFO, O_WRONLY | O_NDELAY);
	if (fd < 0) {
		perror(MM_PLAYCTL_FIFO);
		return 1;
	}
	if (write(fd, &m, sizeof(m)) != (int)sizeof(m)) {
		perror("write");
		close(fd);
		return 1;
	}
	close(fd);
	printf("sent: %d Hz vol %d\n", hz, vol);
	return 0;
}

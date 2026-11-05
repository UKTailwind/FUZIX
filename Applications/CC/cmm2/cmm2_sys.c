/*
 *	/dev/sys for the BASIC runtime on the Colour Maximite 2.
 *
 *	mmb_runtime.c reaches the hardware through one door, an open of
 *	/dev/sys and its ioctls (pico_ioctl.h, the PC3's kernel's requests).
 *	On a PC, pc3host's device server answers them; on the CMM2, bcrun
 *	answers them here, on the firmware's services.
 *
 *	Not yet any: every request is refused as a machine with no such
 *	device refuses it, which the runtime takes as "no display" - drawing
 *	is nothing, and a program that only prints and computes runs.
 */
#include <errno.h>
#include "cmm2.h"

#define CMM2_SYS_FD	100		/* the descriptor /dev/sys is given */

int cmm2_sys_open(void)
{
	return CMM2_SYS_FD;
}

int cmm2_sys_ioctl(int fd, unsigned long code, void *arg)
{
	(void)code;
	(void)arg;
	errno = fd == CMM2_SYS_FD ? ENOTTY : EBADF;
	return -1;
}

/*
 *	ioctl for bcrun on the CMM2, as Fuzix declares it (bcrun.c declares
 *	it again away from Linux).  There are no devices to ask:
 *	bcrun_cmm2.c answers ENOTTY.
 */
#ifndef CMM2_SYS_IOCTL_H
#define CMM2_SYS_IOCTL_H

int ioctl(int, int, ...);

#endif

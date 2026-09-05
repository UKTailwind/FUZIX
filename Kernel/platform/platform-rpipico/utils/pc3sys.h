/*
 * pc3sys.h - the one door to /dev/sys for the programs in this directory.
 *
 * On the board these are the open and the ioctl they replace, nothing
 * more.  Built with PC3_HOST (the pc3host tree, where these programs
 * run on a PC beside bcrun) the same two calls go to the PC3 device
 * server through libpc3client, which answers the same codes with the
 * same structures.  A program includes this instead of naming the
 * device, and knows nothing else about where it is running.
 */
#ifndef PC3SYS_H
#define PC3SYS_H

#ifdef PC3_HOST
int pc3_sys_open(void);
int pc3_sys_ioctl(int fd, unsigned long code, void *arg);
#define pc3_open_sys()          pc3_sys_open()
#define pc3_ioctl(fd, code, a)  pc3_sys_ioctl((fd), (code), (void *)(a))
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#define pc3_open_sys()          open("/dev/sys", O_RDWR)
#define pc3_ioctl(fd, code, a)  ioctl((fd), (code), (a))
#endif

#endif /* PC3SYS_H */

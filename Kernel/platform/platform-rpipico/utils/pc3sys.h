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
int pc3_sys_close(int fd);
#define pc3_open_sys()          pc3_sys_open()
#define pc3_ioctl(fd, code, a)  pc3_sys_ioctl((fd), (code), (void *)(a))
#define pc3_close_sys(fd)       pc3_sys_close(fd)
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#define pc3_open_sys()          open("/dev/sys", O_RDWR)
#define pc3_ioctl(fd, code, a)  ioctl((fd), (code), (a))
#define pc3_close_sys(fd)       close((fd))
#endif

/* The player control FIFO (mmb_playctl.h) and the kind file beside it:
   on Windows the FIFO is a named pipe and /tmp is the temporary
   directory, both in the pc3host tree (hostshim/win32). */
#if defined(PC3_HOST) && defined(_WIN32)
#include <stddef.h>
int pc3w_fifo_server(const char *path);
int pc3w_fifo_read(int fd, void *buf, size_t n);
const char *pc3w_hostpath(const char *path);
#define pc3_fifo_read(fd, b, n) pc3w_fifo_read((fd), (b), (n))
#define pc3_hostpath(p)         pc3w_hostpath(p)
#else
#define pc3_fifo_read(fd, b, n) read((fd), (b), (n))
#define pc3_hostpath(p)         (p)
#endif

#endif /* PC3SYS_H */

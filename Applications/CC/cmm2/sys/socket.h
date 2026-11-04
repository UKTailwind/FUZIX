/*
 *	The CMM2's C library (newlib) has no sockets.  bcrun's socket
 *	libcalls are built against this, and bcrun_cmm2.c answers them
 *	with ENOSYS.
 */
#ifndef CMM2_SYS_SOCKET_H
#define CMM2_SYS_SOCKET_H
#include <stdint.h>
#include <sys/types.h>

typedef uint32_t socklen_t;
typedef uint16_t sa_family_t;

struct sockaddr {
	sa_family_t sa_family;
	char sa_data[14];
};

int socket(int, int, int);
int connect(int, const struct sockaddr *, socklen_t);
int bind(int, const struct sockaddr *, socklen_t);
int listen(int, int);
int accept(int, struct sockaddr *, socklen_t *);
ssize_t sendto(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
ssize_t recvfrom(int, void *, size_t, int, struct sockaddr *, socklen_t *);

#endif

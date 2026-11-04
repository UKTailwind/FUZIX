/*
 *	sockaddr_in for bcrun's socket libcalls on the CMM2 (see
 *	sys/socket.h beside it): the 16 bytes a program builds.
 */
#ifndef CMM2_NETINET_IN_H
#define CMM2_NETINET_IN_H
#include <sys/socket.h>

struct in_addr {
	uint32_t s_addr;
};

struct sockaddr_in {
	sa_family_t sin_family;
	uint16_t sin_port;
	struct in_addr sin_addr;
	unsigned char sin_zero[8];
};

#endif

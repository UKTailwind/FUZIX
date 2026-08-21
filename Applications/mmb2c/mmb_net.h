#ifndef MMB_NET_H
#define MMB_NET_H
/*
 *	The socket floor under every WEB family - constants, address
 *	building, the dotted-quad parser and the non-blocking switch.
 *	PLAN-web.md §2.2; carried only by a program that uses a network
 *	family, and cc1's dead-static rule trims within that.
 *
 *	THE ABI IS FUZIX'S.  A compiled program bakes these constants in,
 *	and the board is the product, so the values are the kernel's
 *	(Library/include/sys/socket.h, fcntl.h) and the board pays no
 *	translation.  The other two worlds adapt:
 *	  - under the host gates' bcrun, the lc_socket family translates
 *	    values in the wrapper (bcrun.c, #ifdef __linux__);
 *	  - a hosted native build (make check) compiles against the
 *	    system headers instead, so the same helper names carry the
 *	    host's own values and glibc is called directly.
 *
 *	Everything program-facing goes through the mmn_* helpers below,
 *	so the split lives in this file and nowhere else.  Both targets
 *	are little-endian, which mmn_sin's byte stores rely on.
 */

#include <string.h>
#include "mmb_runtime.h"

#ifndef MMG_FN
#if defined(MM_FCC) || defined(MM_PC3)
#define MMG_FN static
#else
#define MMG_FN static __inline__ __attribute__((unused))
#endif
#endif

#if defined(MM_FCC) || defined(MM_PC3)

/* sys/socket.h, netinet/in.h, fcntl.h - the kernel's values */
#define MMN_AF_INET	1
#define MMN_SOCK_RAW	1
#define MMN_SOCK_DGRAM	2
#define MMN_SOCK_STREAM	3
#define MMN_IPPROTO_TCP	6
#define MMN_IPPROTO_UDP	17
#define MMN_IPPROTO_TLS	254	/* Kernel/include/net_lwip.h */
#define MMN_O_NDELAY	16
#define MMN_F_GETFL	0
#define MMN_F_SETFL	1
#define MMN_SIOCTLSHOST	0x0420

/* Resolved by name: the board's libc, or bcrun's lc_ wrappers. */
int socket(int __d, int __t, int __p);
int connect(int __fd, void *__sa, int __len);
int bind(int __fd, void *__sa, int __len);
int listen(int __fd, int __n);
int accept(int __fd, void *__sa, void *__lenp);
int sendto(int __fd, void *__buf, int __n, int __fl, void *__sa, int __len);
int recvfrom(int __fd, void *__buf, int __n, int __fl, void *__sa,
	     void *__lenp);
int ioctl(int __fd, int __req, void *__p);
int fcntl(int __fd, int __cmd, int __v);
int close(int __fd);
int read(int __fd, void *__buf, int __n);
int write(int __fd, void *__buf, int __n);

#define mmn_socket(d, t, p)		socket(d, t, p)
#define mmn_connect(fd, sa, l)		connect(fd, (void *)(sa), l)
#define mmn_bind(fd, sa, l)		bind(fd, (void *)(sa), l)
#define mmn_listen(fd, n)		listen(fd, n)
#define mmn_accept(fd, sa, lp)		accept(fd, (void *)(sa), (void *)(lp))
#define mmn_sendto(fd, b, n, f, sa, l)	\
	sendto(fd, (void *)(b), n, f, (void *)(sa), l)
#define mmn_recvfrom(fd, b, n, f, sa, lp) \
	recvfrom(fd, (void *)(b), n, f, (void *)(sa), (void *)(lp))
#define mmn_ioctl(fd, r, p)		ioctl(fd, r, (void *)(p))
#define mmn_fcntl(fd, c, v)		fcntl(fd, c, v)
#define mmn_close(fd)			close(fd)
#define mmn_read(fd, b, n)		read(fd, (void *)(b), n)
#define mmn_write(fd, b, n)		write(fd, (void *)(b), n)

#else /* hosted native: the system's own sockets */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#define MMN_AF_INET	AF_INET
#define MMN_SOCK_RAW	SOCK_RAW
#define MMN_SOCK_DGRAM	SOCK_DGRAM
#define MMN_SOCK_STREAM	SOCK_STREAM
#define MMN_IPPROTO_TCP	IPPROTO_TCP
#define MMN_IPPROTO_UDP	IPPROTO_UDP
#define MMN_IPPROTO_TLS	0	/* TLS is kernel-side on the board only */
#define MMN_O_NDELAY	O_NONBLOCK
#define MMN_F_GETFL	F_GETFL
#define MMN_F_SETFL	F_SETFL
#define MMN_SIOCTLSHOST	0x0420

#define mmn_socket(d, t, p)		socket(d, t, p)
#define mmn_connect(fd, sa, l)		\
	connect(fd, (const struct sockaddr *)(void *)(sa), l)
#define mmn_bind(fd, sa, l)		\
	bind(fd, (const struct sockaddr *)(void *)(sa), l)
#define mmn_listen(fd, n)		listen(fd, n)
#define mmn_accept(fd, sa, lp)		\
	accept(fd, (struct sockaddr *)(void *)(sa), (socklen_t *)(void *)(lp))
#define mmn_sendto(fd, b, n, f, sa, l)	\
	sendto(fd, b, n, f, (const struct sockaddr *)(void *)(sa), l)
#define mmn_recvfrom(fd, b, n, f, sa, lp) \
	recvfrom(fd, b, n, f, (struct sockaddr *)(void *)(sa), \
		 (socklen_t *)(void *)(lp))
/* The board-only requests succeed silently, so the same program
   structure-tests here; anything else is real. */
MMG_FN int mmn_ioctl(int fd, int req, void *p)
{
	if (req == MMN_SIOCTLSHOST || (req >= 0x0040 && req <= 0x0043))
		return 0;
	return ioctl(fd, req, p);
}
#define mmn_fcntl(fd, c, v)		fcntl(fd, c, v)
#define mmn_close(fd)			close(fd)
#define mmn_read(fd, b, n)		read(fd, (void *)(b), n)
#define mmn_write(fd, b, n)		write(fd, (void *)(b), n)

#endif /* hosted */

/*
 *	struct sockaddr_in, built by hand into 16 bytes so no struct
 *	layout crosses a compiler boundary: family (uint16,
 *	little-endian - both targets are), port and address in network
 *	order.  The layout is BSD's and is byte-identical on Fuzix and
 *	Linux; only the family VALUE differs, and MMN_AF_INET carries
 *	the right one for the build.
 */
MMG_FN void mmn_sin(unsigned char *sa, const unsigned char *ip4, int port)
{
	memset(sa, 0, 16);
	sa[0] = (unsigned char)(MMN_AF_INET & 0xFF);
	sa[1] = (unsigned char)((MMN_AF_INET >> 8) & 0xFF);
	sa[2] = (unsigned char)((port >> 8) & 0xFF);
	sa[3] = (unsigned char)(port & 0xFF);
	if (ip4)
		memcpy(sa + 4, ip4, 4);
}

/*
 *	Dotted quad out of an M-string, network-order bytes out.
 *	Deliberately strict - exactly four decimal fields, each 0-255 -
 *	because everything that is NOT a dotted quad goes to the
 *	resolver (stage 2), and a sloppy parse here would eat hostnames.
 *	The WebMite's own gate is "three dots and ip4addr_aton agrees"
 *	(MMTCPclient.c:303-306); this is that test.
 */
MMG_FN int mmn_aton(const char *m, unsigned char *ip4)
{
	int len = mm_slen(m), i = 1, f, v, digits;

	for (f = 0; f < 4; f++) {
		v = 0;
		digits = 0;
		while (i <= len && m[i] >= '0' && m[i] <= '9') {
			v = v * 10 + (m[i] - '0');
			if (v > 255)
				return 0;
			i++;
			digits++;
		}
		if (digits == 0)
			return 0;
		ip4[f] = (unsigned char)v;
		if (f < 3) {
			if (i > len || m[i] != '.')
				return 0;
			i++;
		}
	}
	return i == len + 1;
}

/*	Append one address byte as decimal - the addressbuff formatter's
 *	worker, hand-rolled so no printf prototype is needed here. */
MMG_FN int mmn_decb(char *out, unsigned int v)
{
	int n = 0;

	if (v >= 100)
		out[n++] = (char)('0' + v / 100);
	if (v >= 10)
		out[n++] = (char)('0' + (v / 10) % 10);
	out[n++] = (char)('0' + v % 10);
	return n;
}

MMG_FN void mmn_ndelay(int fd)
{
	mmn_fcntl(fd, MMN_F_SETFL, mmn_fcntl(fd, MMN_F_GETFL, 0)
		  | MMN_O_NDELAY);
}

#endif /* MMB_NET_H */

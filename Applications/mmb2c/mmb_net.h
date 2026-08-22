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
#define MMN_O_RDWR	2
#define MMN_NETIOC_TLSCA 0x0043	/* pico_ioctl.h is the authority */
#define MMN_NETIOC_STATUS 0x0041

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
int open(const char *__path, int __flags);
int close(int __fd);
int read(int __fd, void *__buf, int __n);
int write(int __fd, void *__buf, int __n);

#define mmn_open_ro(p)			open(p, 0)	/* O_RDONLY is 0 */
#define mmn_open_rw(p)			open(p, MMN_O_RDWR)

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
#define MMN_O_RDWR	O_RDWR
#define MMN_NETIOC_TLSCA 0x0043
#define MMN_NETIOC_STATUS 0x0041

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
#define mmn_open_ro(p)			open(p, O_RDONLY)
#define mmn_open_rw(p)			open(p, O_RDWR)
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

/*
 *	Wrap-safe deadline arithmetic.  mm_us() crosses the libcall
 *	boundary in 31 bits on the board, so deadlines are never compared
 *	as absolutes: take a start, subtract unsigned, and the difference
 *	is right across the wrap.  (unsigned long is 32 bits there, 64 on
 *	the host - correct in both.)
 */
#define MMN_US()		((unsigned long)mm_us())
#define MMN_SINCE(t0)		((unsigned long)(MMN_US() - (t0)))

/*
 *	The resolver - Applications/netd/gethostbyname.c re-expressed as
 *	statics, the PLAN-web.md §5 bargain: DNS over the sendto/recvfrom
 *	doors, compiled only into a program that names a host.  Recursive
 *	A queries only, like the original.
 */

/*	First word of each /etc/resolv.conf "nameserver" line, cached:
 *	the file does not change under a running program, and a compile
 *	of the parse per lookup would be pure waste. */
static unsigned char mmn_ns[4];
static signed char mmn_ns_state;	/* 0 unread, 1 good, -1 none */

MMG_FN int mmn_aton_span(const char *p, int len, unsigned char *ip4)
{
	char m[20];

	if (len < 7 || len > 15)
		return 0;
	m[0] = (char)len;
	memcpy(m + 1, p, len);
	return mmn_aton(m, ip4);
}

MMG_FN int mmn_nameserver(unsigned char *ip4)
{
	char buf[257];
	int fd, n, i, j, k;

	if (mmn_ns_state == 0) {
		mmn_ns_state = -1;
		fd = mmn_open_ro("/etc/resolv.conf");
		if (fd >= 0) {
			n = mmn_read(fd, buf, 256);
			mmn_close(fd);
			if (n < 0)
				n = 0;
			buf[n] = 0;
			for (i = 0; i < n; ) {
				/* one line: [ws] nameserver [ws] a.b.c.d */
				j = i;
				while (j < n && buf[j] != '\n')
					j++;
				while (i < j && (buf[i] == ' ' || buf[i] == '\t'))
					i++;
				if (j - i > 11 &&
				    memcmp(buf + i, "nameserver", 10) == 0 &&
				    (buf[i + 10] == ' ' || buf[i + 10] == '\t')) {
					i += 10;
					while (i < j && (buf[i] == ' ' || buf[i] == '\t'))
						i++;
					k = i;
					while (k < j && buf[k] > ' ')
						k++;
					if (mmn_aton_span(buf + i, k - i, mmn_ns)) {
						mmn_ns_state = 1;
						break;
					}
				}
				i = j + 1;
			}
		}
	}
	if (mmn_ns_state != 1)
		return 0;
	memcpy(ip4, mmn_ns, 4);
	return 1;
}

/*
 *	MM.INFO(IP ADDRESS) - the machine's address out of NETIOC_STATUS
 *	on /dev/sys, "0.0.0.0" when there is no radio, no join, or no
 *	/dev/sys (the hosted gates).  net_status.ip is HOST order, and
 *	wifi(8) prints it high byte first - so bytes 15..12 of the
 *	little-endian struct are a.b.c.d, in that order.
 */
static char mmn_ipbuf[17];

MMG_FN char *mmn_ipaddr(void)
{
	unsigned char st[48];
	int sys, i;

	memset(st, 0, sizeof(st));
	sys = mmn_open_rw("/dev/sys");
	if (sys >= 0) {
		mmn_ioctl(sys, MMN_NETIOC_STATUS, st);
		mmn_close(sys);
	}
	i = 1;
	i += mmn_decb(mmn_ipbuf + i, st[15]);
	mmn_ipbuf[i++] = '.';
	i += mmn_decb(mmn_ipbuf + i, st[14]);
	mmn_ipbuf[i++] = '.';
	i += mmn_decb(mmn_ipbuf + i, st[13]);
	mmn_ipbuf[i++] = '.';
	i += mmn_decb(mmn_ipbuf + i, st[12]);
	mmn_ipbuf[0] = (char)(i - 1);
	return mmn_ipbuf;
}

/*	1 resolved, 0 no answer within the timeout, -1 no nameserver /
 *	no socket.  The query id comes off the clock so a stale reply
 *	from an earlier attempt is never mistaken for this one. */
MMG_FN int mmn_resolve(const char *host, unsigned char *ip4,
		       long tmo_ms)
{
	unsigned char q[300], r[512], ns[4], sa[16], src[16];
	int fd, qn, i, n, len, labels, ancount, ty, rdlen;
	int hl = mm_slen(host);
	unsigned int id;
	unsigned long t0, lastsend;
	int sl;

	if (hl < 1 || hl > 200)
		return -1;
	if (!mmn_nameserver(ns))
		return -1;
	fd = mmn_socket(MMN_AF_INET, MMN_SOCK_DGRAM, MMN_IPPROTO_UDP);
	if (fd < 0)
		return -1;
	mmn_ndelay(fd);
	mmn_sin(sa, ns, 53);

	id = (unsigned int)MMN_US() & 0xFFFF;
	memset(q, 0, 12);
	q[0] = (unsigned char)(id >> 8);
	q[1] = (unsigned char)id;
	q[2] = 0x01;			/* RD */
	q[5] = 1;			/* QDCOUNT */
	qn = 12;
	/* the name, as length-prefixed labels */
	i = 1;
	while (i <= hl) {
		labels = 0;
		len = qn++;
		while (i <= hl && host[i] != '.' && labels < 63) {
			q[qn++] = (unsigned char)host[i++];
			labels++;
		}
		q[len] = (unsigned char)labels;
		if (labels == 0) {
			mmn_close(fd);
			return -1;	/* "..", or a trailing dot */
		}
		if (i <= hl && host[i] == '.')
			i++;
	}
	q[qn++] = 0;
	q[qn++] = 0; q[qn++] = 1;	/* QTYPE A */
	q[qn++] = 0; q[qn++] = 1;	/* QCLASS IN */

	t0 = MMN_US();
	lastsend = t0 - 2000000UL;	/* so the first send happens now */
	while ((long)MMN_SINCE(t0) < tmo_ms * 1000L) {
		if (MMN_SINCE(lastsend) >= 1500000UL) {
			lastsend = MMN_US();
			mmn_sendto(fd, q, qn, 0, sa, 16);
		}
		sl = 16;
		n = mmn_recvfrom(fd, r, (int)sizeof(r), 0, src, &sl);
		if (n < 12) {
			mm_pause(2.0);
			continue;
		}
		if (r[0] != (unsigned char)(id >> 8) ||
		    r[1] != (unsigned char)id)
			continue;
		ancount = (r[6] << 8) | r[7];
		/* skip the question we asked */
		i = 12;
		while (i < n && r[i] != 0)
			i += r[i] + 1;
		i += 5;
		while (ancount-- > 0 && i + 10 <= n) {
			/* the answer's name: a compression pointer or
			   labels */
			if (r[i] & 0xC0)
				i += 2;
			else {
				while (i < n && r[i] != 0)
					i += r[i] + 1;
				i++;
			}
			if (i + 10 > n)
				break;
			ty = (r[i] << 8) | r[i + 1];
			rdlen = (r[i + 8] << 8) | r[i + 9];
			i += 10;
			if (ty == 1 && rdlen == 4 && i + 4 <= n) {
				memcpy(ip4, r + i, 4);
				mmn_close(fd);
				return 1;
			}
			i += rdlen;
		}
		/* a reply with no A record is a real answer: NO */
		mmn_close(fd);
		return 0;
	}
	mmn_close(fd);
	return 0;
}

#endif /* MMB_NET_H */

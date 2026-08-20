#ifndef _NET_LWIP_H
#define _NET_LWIP_H

/*
 *	The seam between Fuzix's socket layer and an lwIP stack.
 *
 *	net_lwip.c implements the netproto_* hooks in terms of these, and
 *	knows nothing about lwIP.  The platform implements them and knows
 *	nothing about Fuzix.  That split is not tidiness: lwIP drags in
 *	the C library's <stdio.h> and <sys/time.h>, whose ssize_t and
 *	time_t are not the kernel's, so a single file containing both
 *	sets of headers does not compile.  On the Pico Computer 3 the
 *	implementation is in platform-rpipico/net_cyw43.c.
 *
 *	Addresses and ports cross this line in NETWORK order, because
 *	that is how both sides already hold them - struct sockaddr_in on
 *	one side and ip4_addr_t on the other.  The platform converts
 *	where lwIP wants a host-order port.
 *
 *	Buffers cross it as ordinary pointers, which on a
 *	CONFIG_USERMEM_DIRECT platform may be user addresses that
 *	valaddr() has already checked.  A platform with banked or
 *	separate user memory would need uget/uput here instead.
 */

#include <stdint.h>

/*
 *	Must match NSOCKET in netdev.h.  The platform side cannot include
 *	netdev.h - it is a kernel header - so it gets the size from here,
 *	and net_lwip.c fails the build if the two ever disagree.
 */
#define NETLW_NSOCKET	8

/*
 * TLS is asked for as a protocol on a stream socket:
 *
 *	socket(AF_INET, SOCK_STREAM, IPPROTO_TLS)
 *
 * 254 is one of the two numbers RFC 3692 reserves for experiments, so
 * it cannot collide with a real protocol.  A private number is the
 * only place to put this: Fuzix has no setsockopt, so there is nowhere
 * to say "upgrade this socket" after the fact.
 */
#define IPPROTO_TLS	254

/* Per-socket ioctl, before connect: the SNI name and what the peer's
   certificate is checked against.  In the SIOC* number space. */
#define SIOCTLSHOST	0x0420

/* Errors, so that neither side has to know the other's errno */
#define NETLW_OK	0
#define NETLW_NOMEM	(-1)
#define NETLW_INUSE	(-2)
#define NETLW_DOWN	(-3)
#define NETLW_EMPTY	(-4)	/* recv: nothing queued */
#define NETLW_BIG	(-5)	/* send: datagram too large */
#define NETLW_EOF	(-6)	/* recv: the peer closed */
#define NETLW_RESET	(-7)	/* the connection died */

/* Provided by the platform */
int netlw_isup(void);
uint32_t netlw_myip(void);
void netlw_free(uint8_t slot);
void netlw_peer(uint8_t slot, uint32_t *ip, uint16_t *port);

int netlw_udp_new(uint8_t slot);
int netlw_udp_bind(uint8_t slot, uint32_t ip, uint16_t *port);
int netlw_udp_send(uint8_t slot, const void *buf, uint16_t len,
		   uint32_t ip, uint16_t port);
int netlw_udp_recv(uint8_t slot, void *buf, uint16_t max,
		   uint32_t *ip, uint16_t *port);

/*
 *	Raw sockets, which on this machine means ping(1).  Send takes the
 *	payload only - lwIP builds the IP header, as a BSD raw socket
 *	without IP_HDRINCL does - and receive hands back the whole IP
 *	packet, header included, because that is what ping parses.
 */
int netlw_raw_new(uint8_t slot, uint8_t proto);
int netlw_raw_send(uint8_t slot, const void *buf, uint16_t len, uint32_t ip);
int netlw_raw_recv(uint8_t slot, void *buf, uint16_t max, uint32_t *ip);

int netlw_tcp_new(uint8_t slot);
/* TLS, client side.  netlw_tls_host must be called before connect:
   it is the SNI name and what the certificate is checked against,
   and connect() carries an address and nothing else. */
int netlw_tls_new(uint8_t slot);
int netlw_tls_host(uint8_t slot, const char *name);
int netlw_tls_ca(const void *ca, unsigned len);
int netlw_tcp_bind(uint8_t slot, uint32_t ip, uint16_t *port);
int netlw_tcp_connect(uint8_t slot, uint32_t ip, uint16_t port);
int netlw_tcp_listen(uint8_t slot);
/* Returns bytes accepted - which may be fewer than offered, or 0 when
   the send window is full - or a negative NETLW_ code. */
int netlw_tcp_send(uint8_t slot, const void *buf, uint16_t len);
int netlw_tcp_recv(uint8_t slot, void *buf, uint16_t max);
void netlw_tcp_close(uint8_t slot);

/*
 *	Provided by net_lwip.c and called from the platform's poll
 *	context.  This is the whole of the upward interface: everything
 *	that happens to a connection while nobody is in a syscall
 *	arrives through one of these.
 */
void netlw_wake(uint8_t slot);		/* data or room; wake a sleeper */
/* The kernel clock in seconds.  TLS needs it to decide whether a
   certificate has expired, so a machine with a wrong clock cannot
   verify one - which is why this port bothers with a DS3231 and
   ntpdate. */
uint32_t netlw_now(void);
/* DIAGNOSTIC: the lowest word the kernel stack may use, which is just
   above udata - the stack grows down into it, so overflowing means
   corrupting the process table.  Used to paint the stack and measure
   how deep the pump actually goes. */
uint32_t *netlw_kstack_bottom(void);
uint32_t *netlw_kstack_top(void);
void netlw_connected(uint8_t slot);	/* connect() completed */
void netlw_closed(uint8_t slot);	/* peer sent FIN */
void netlw_reset(uint8_t slot, int err);/* connection died; pcb is gone */
/* An incoming connection: returns a socket slot for it, or -1 if the
   table is full, in which case the platform refuses the connection. */
int netlw_accept_slot(uint8_t listener);

#endif

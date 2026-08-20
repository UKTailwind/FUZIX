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

/* Errors, so that neither side has to know the other's errno */
#define NETLW_OK	0
#define NETLW_NOMEM	(-1)
#define NETLW_INUSE	(-2)
#define NETLW_DOWN	(-3)
#define NETLW_EMPTY	(-4)	/* recv: nothing queued */
#define NETLW_BIG	(-5)	/* send: datagram too large */

/* Provided by the platform */
int netlw_isup(void);
uint32_t netlw_myip(void);
int netlw_udp_new(uint8_t slot);
void netlw_udp_free(uint8_t slot);
int netlw_udp_bind(uint8_t slot, uint32_t ip, uint16_t *port);
int netlw_udp_send(uint8_t slot, const void *buf, uint16_t len,
		   uint32_t ip, uint16_t port);
int netlw_udp_recv(uint8_t slot, void *buf, uint16_t max,
		   uint32_t *ip, uint16_t *port);

/* Provided by net_lwip.c, called from the platform's poll context when
   a datagram arrives: wakes anyone sleeping in recvfrom. */
void netlw_wake(uint8_t slot);

#endif

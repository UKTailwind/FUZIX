/*
 *	Fuzix sockets over an lwIP stack.
 *
 *	This is the netproto_* backend that network.c and syscall_net.c
 *	call.  It is the same shape as net_w5x00.c and for the same
 *	reason: the socket layer keeps the state machine and the thing
 *	underneath owns the buffers.  There the thing underneath is a
 *	W5500 with 32K of its own; here it is lwIP, which happens to run
 *	on the same CPU but is otherwise treated exactly like an offload
 *	engine - this file never sees a pbuf.
 *
 *	Everything lwIP is behind net_lwip.h, which the platform
 *	implements.  See that header for why the split has to exist at
 *	all (it is the C library's headers, not taste).
 *
 *	DATAGRAMS ONLY, for now.  UDP is what ntpdate, dig and the
 *	resolver need, and it is the half of the socket state machine
 *	that does not need a connection: bind, sendto, recvfrom.  TCP is
 *	the next piece of work and the stubs below say so rather than
 *	pretending.
 */

#include <kernel.h>
#include <kdata.h>
#include <netdev.h>
#include <net_lwip.h>
#include <printf.h>

#ifdef CONFIG_NET_LWIP

#if NETLW_NSOCKET != NSOCKET
#error "net_lwip.h and netdev.h disagree about the socket count"
#endif

#define is_datagram(s)	((s)->s_class != SOCK_STREAM && (s)->s_class != SOCK_SEQPACKET)

uint8_t sock_wake[NSOCKET];

/*
 *	Ephemeral ports.  Same range and the same reason as net_w5x00.c:
 *	above anything a server binds and below the 16 bit wrap.
 */
static uint16_t autoport = 5000;

static void inc_autoport(void)
{
	autoport++;
	if (autoport == 32767)
		autoport = 5000;
}

/*
 *	The wake-up from the receive path.  This runs in the platform's
 *	poll context - thread mode, in the kernel's own pump - so it may
 *	call wakeup(), exactly as a device interrupt handler would.
 */
void netlw_wake(uint8_t slot)
{
	sock_wake[slot] = 1;
	wakeup(sock_wake + slot);
}

static struct socket *netproto_create(void)
{
	register struct socket *s = sockets;
	uint_fast8_t i;

	for (i = 0; i < NSOCKET; i++) {
		if (s->s_state == SS_UNUSED) {
			s->s_num = i;
			s->s_parent = 0xFF;
			s->proto.slot = i;
			return s;
		}
		s++;
	}
	return NULL;
}

/*
 *	Only the families and types this backend can actually do.  A
 *	socket() for something else has to fail here rather than at the
 *	first send: a program that gets a file descriptor believes it.
 */
int netproto_socket(void)
{
	register struct socket *s;

	if (!netlw_isup()) {
		udata.u_error = ENETDOWN;
		return 0;
	}
	if (udata.u_net.args[1] != AF_INET) {
		udata.u_error = EAFNOSUPPORT;
		return 0;
	}
	if (udata.u_net.args[2] != SOCK_DGRAM ||
	    (udata.u_net.args[3] != 0 &&
	     udata.u_net.args[3] != IPPROTO_UDP)) {
		/* SOCK_STREAM lands here until the TCP half exists.  An
		   honest EPROTONOSUPPORT is better than a socket that
		   accepts a connect() and never connects. */
		udata.u_error = EPROTONOSUPPORT;
		return 0;
	}
	s = netproto_create();
	if (s == NULL) {
		udata.u_error = ENFILE;
		return 0;
	}
	if (netlw_udp_new(s->proto.slot)) {
		udata.u_error = ENOBUFS;
		return 0;
	}
	net_setup(s);
	s->s_class = udata.u_net.args[2];
	s->s_state = SS_UNCONNECTED;
	s->s_type = SOCK_DGRAM;
	udata.u_net.sock = s->s_num;
	return 0;
}

void netproto_setup(struct socket *s)
{
	used(s);
}

void netproto_free(struct socket *s)
{
	netlw_udp_free(s->proto.slot);
	s->s_state = SS_UNUSED;
}

/*
 *	Is this local address already taken?  The core asks before a
 *	bind, so the answer has to come from the socket table rather
 *	than from lwIP: lwIP would happily give us a second binding and
 *	the core's EADDRINUSE would never fire.
 */
int netproto_find_local(struct ksockaddr *ka)
{
	register struct socket *s = sockets;
	uint_fast8_t n;

	for (n = 0; n < NSOCKET; n++) {
		if (s->s_state >= SS_BOUND &&
		    s->src_addr.sa.family == AF_INET &&
		    s->src_addr.sa.sin.sin_port == ka->sa.sin.sin_port) {
			if (s->src_addr.sa.sin.sin_addr.s_addr ==
			    ka->sa.sin.sin_addr.s_addr ||
			    s->src_addr.sa.sin.sin_addr.s_addr == 0)
				return n;
		}
		s++;
	}
	return -1;
}

static int do_bind(struct socket *s)
{
	uint16_t port = s->src_addr.sa.sin.sin_port;

	if (netlw_udp_bind(s->proto.slot,
			   s->src_addr.sa.sin.sin_addr.s_addr, &port)) {
		udata.u_error = EADDRINUSE;
		return -1;
	}
	/* lwIP picks the port when we ask for 0; record what it chose so
	   getsockname and the duplicate check both see the truth. */
	s->src_addr.sa.sin.sin_port = port;
	s->src_addr.sa.family = AF_INET;
	s->src_len = sizeof(struct sockaddr_in);
	s->s_state = SS_BOUND;
	return 0;
}

int netproto_autobind(struct socket *s)
{
	s->src_addr.sa.family = AF_INET;
	s->src_addr.sa.sin.sin_addr.s_addr = 0;
	do {
		s->src_addr.sa.sin.sin_port = ntohs(autoport);	/* a swap is a swap */
		inc_autoport();
	} while (netproto_find_local(&s->src_addr) != -1);
	return do_bind(s);
}

int netproto_bind(struct socket *s)
{
	if (udata.u_net.addrbuf.sa.family != AF_INET) {
		udata.u_error = EAFNOSUPPORT;
		return 0;
	}
	if (udata.u_net.addrbuf.sa.sin.sin_addr.s_addr &&
	    udata.u_net.addrbuf.sa.sin.sin_addr.s_addr != netlw_myip()) {
		udata.u_error = EADDRNOTAVAIL;
		return 0;
	}
	if (ntohs(udata.u_net.addrbuf.sa.sin.sin_port) < 1024 &&
	    udata.u_euid != 0) {
		udata.u_error = EACCES;
		return 0;
	}
	memcpy(&s->src_addr, &udata.u_net.addrbuf, sizeof(struct ksockaddr));
	do_bind(s);
	return 0;
}

/*
 *	connect() on a datagram socket only records where sends go, so
 *	it completes here and now.  The core returns 1 after calling
 *	this - it is written for a stack that has to wait for a SYN - so
 *	set the wake flag or run_sockfunc would sleep waiting for an
 *	event that has already happened.
 */
int netproto_begin_connect(struct socket *s)
{
	if (udata.u_net.addrbuf.sa.family != AF_INET) {
		udata.u_error = EAFNOSUPPORT;
		return 0;
	}
	memcpy(&s->dst_addr, &udata.u_net.addrbuf, sizeof(struct ksockaddr));
	s->dst_len = sizeof(struct sockaddr_in);
	s->s_state = SS_CONNECTED;
	sock_wake[s->s_num] = 1;
	return 0;
}

arg_t netproto_write(struct socket *s, struct ksockaddr *ka)
{
	int r;

	/*
	 *	network.c's net_write() - the write() and send() path - hands
	 *	us s->src_addr as the destination.  For a stack where the
	 *	connection carries the address that is harmless and every
	 *	other backend ignores the argument; for connected UDP it
	 *	means posting the datagram to ourselves.  Use the address
	 *	connect() actually recorded.
	 *
	 *	The right fix is in network.c, but that is shared with every
	 *	other port and this is the only backend that can see the
	 *	difference, so it is worth reporting upstream before
	 *	changing it there.
	 */
	if (ka == &s->src_addr && s->s_state == SS_CONNECTED && s->dst_len)
		ka = &s->dst_addr;

	if (ka->sa.family != AF_INET || ka->sa.sin.sin_addr.s_addr == 0) {
		udata.u_error = EDESTADDRREQ;
		return 0;
	}
	if (udata.u_count > 1472) {
		udata.u_error = EMSGSIZE;
		return 0;
	}
	r = netlw_udp_send(s->proto.slot, udata.u_base, udata.u_count,
			   ka->sa.sin.sin_addr.s_addr, ka->sa.sin.sin_port);
	if (r == NETLW_NOMEM) {
		/* Out of pbufs: the pump will free some.  Sleeping here is
		   what the core's return-1 contract is for. */
		return 1;
	}
	if (r < 0) {
		udata.u_error = (r == NETLW_DOWN) ? ENETDOWN : EIO;
		return 0;
	}
	udata.u_done += udata.u_count;
	udata.u_base += udata.u_count;
	return 0;
}

int netproto_read(struct socket *s)
{
	uint32_t ip;
	uint16_t port;
	int r;

	if (udata.u_count == 0)
		return 0;

	r = netlw_udp_recv(s->proto.slot, udata.u_base, udata.u_count,
			   &ip, &port);
	if (r == NETLW_EMPTY) {
		if (s->s_iflags & SI_EOF)
			return 0;
		return 1;		/* sleep, the receive callback wakes us */
	}
	if (r < 0) {
		udata.u_error = EIO;
		return 0;
	}
	/* recvfrom wants to know who sent it; the core copies this out. */
	udata.u_net.addrbuf.sa.family = AF_INET;
	udata.u_net.addrbuf.sa.sin.sin_family = AF_INET;
	udata.u_net.addrbuf.sa.sin.sin_addr.s_addr = ip;
	udata.u_net.addrbuf.sa.sin.sin_port = port;
	udata.u_net.addrlen = sizeof(struct sockaddr_in);
	udata.u_done += r;
	udata.u_base += r;
	return 0;
}

int netproto_close(struct socket *s)
{
	netproto_free(s);
	return 0;
}

arg_t netproto_ioctl(struct socket *s, int req, char *data)
{
	used(s);
	used(req);
	used(data);
	/* The SIOCxIF* set belongs here.  Nothing needs it yet: the
	   address comes from DHCP and the wifi command reports it. */
	udata.u_error = EINVAL;
	return -1;
}

/*
 *	The stream half.  netproto_socket refuses SOCK_STREAM, so none of
 *	these can be reached; they exist because netdev.h says they must
 *	and because an empty function is a clearer statement of what is
 *	missing than a link error.
 */
int netproto_listen(struct socket *s)
{
	used(s);
	udata.u_error = EOPNOTSUPP;
	return 0;
}

int netproto_accept(struct socket *s)
{
	used(s);
	udata.u_error = EOPNOTSUPP;
	return 0;
}

int netproto_accept_complete(struct socket *s)
{
	used(s);
	return 0;
}

struct socket *netproto_sockpending(struct socket *s)
{
	used(s);
	return NULL;
}

arg_t netproto_shutdown(struct socket *s, uint8_t how)
{
	used(how);
	s->s_iflags |= SI_SHUTR | SI_SHUTW;
	return 0;
}

void netdev_init(void)
{
	uint_fast8_t i;

	for (i = 0; i < NSOCKET; i++)
		sockets[i].s_state = SS_UNUSED;
}

#endif

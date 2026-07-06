/*
 *	M4Board stack driver, based on the WizNet driver
 *
 */

#include <kernel.h>

#ifdef CONFIG_NET_M4BOARD

#include <kdata.h>
#include <printf.h>
#include <netdev.h>
#include <net_m4board.h>


/* For now until we work out where this really belongs */
uint8_t sock_wake[NSOCKET];

#define M4_USER_SOCKETS 4
#define M4_SLOT_FIRST   1
#define M4_SLOT_LAST    4
#define M4_INVALID_SLOT 0xFF

#define M4_SS_IDLE			0
#define M4_SS_CONNECTING	1
#define M4_SS_SENDING		2
#define M4_SS_RCLOSED		3
#define M4_SS_WAITINGINC	4
#define M4_SS_DNSLOOKING	5

#define M4_SLC_NONE			0
#define M4_SLC_SEND			1
#define M4_SLC_DNSLOOKUP	2
#define M4_SLC_CONNECT		3
#define M4_SLC_ACCEPT		4
#define M4_SLC_RECV			5
#define M4_SLC_ERRHAND		6

/* Offset conversions: in the W5100 into a linear 16bit space */

static uint8_t sock_free = (1U << M4_USER_SOCKETS) - 1;
static uint8_t m42sock_map[M4_USER_SOCKETS] = {M4_INVALID_SLOT, M4_INVALID_SLOT, M4_INVALID_SLOT, M4_INVALID_SLOT};

/*interface state*/

static uint32_t ipa;
static uint32_t iga;
static uint32_t igm;
static uint8_t mac[6] = { 0xC0, 0xFF, 0xEE, 0xC0, 0xFF, 0xEE };
static uint16_t mtu = 1500;
static uint16_t ifflags = IFF_BROADCAST | IFF_RUNNING | IFF_UP;
static uint16_t autoport = 5000; /* ntohs(5000);*/

extern struct m4_network_config m4_network_config;

/* Must only be called by the IRQ path */
static void m4_eof(struct socket *s)
{
	s->s_iflags |= SI_EOF;
	s->s_wake = 1;
}

/* State management for creation of a socket. */
static int net_alloc(void)
{
	register uint_fast8_t i = 0;
	register uint_fast8_t j = 1;
	while(i < NSOCKET) {
		if (sock_free & j) {
			sock_free &= ~j;
			return i;
		}
		j <<= 1;
		i++;
	}
	return -1;
}

/*
 *	Allocate a socket and bind it to a physical socket. Update the
 *	map tables in the process.
 */
static struct socket *netproto_create(void)
{
	int n;
	register int i;
	register struct socket *s;
	for (i = 0; i < M4_USER_SOCKETS; i++) {
		if (m42sock_map[i] == M4_INVALID_SLOT) {
			n = net_alloc();
			s = sockets + n;
			/* Some of this belong sin core code ? */
			s->s_num = n;
			s->s_parent = 0xFF;
			s->proto.slot = i;
			m42sock_map[i] = n;
			return s;
		}
	}
	return NULL;
}

/*
 *	Mark a socket as free and release the associated wiznet channel
 */
void netproto_free(struct socket *s)
{
	m42sock_map[s->proto.slot] = M4_INVALID_SLOT;
	sock_free |= (1U << s->s_num);
}

/* Until we do incoming socket support */
struct socket *netproto_sockpending(struct socket *s)
{
	register struct socket *n = sockets;
	register uint_fast8_t id = s->s_num;
	int i;
	for (i = 0; i < NSOCKET; i++) {
		if (n->s_state != SS_UNUSED && n->s_parent == id) {
			n->s_parent = 0xFF;
			return n;
		}
		n++;
	}
	return NULL;
}

/*
 *	Used when a socket is finally cleaned up and nobody either side
 *	wants it.
 */
static void netproto_cleanup(struct socket *s)
{
	m4_net_close_socket = s->proto.slot;

	m4_net_close();
	s->s_state = SS_UNUSED;
	netproto_free(s);
}

/* Bind a socket to an address. */
/* We arrange the internal types we use to match the chip
   - 21 TCP UDP/RAW nott supported

   The higher level stuff is done further done in netproto_bind, this
   function merely handles the poking of the device */
static int do_netproto_bind(struct socket *s)
{
	if (s->s_class != SOCK_STREAM || (s->s_protocol != 0 && s->s_protocol != IPPROTO_TCP)) {
        udata.u_error = EOPNOTSUPP;
        return -1;
    }
	m4_net_bind_socket = s->proto.slot;
	m4_net_bind_port = s->src_addr.sa.sin.sin_port;
	if (m4_net_bind() != 0) {
	   udata.u_error = EADDRINUSE;
        return -1;
    }		
	s->s_state = SS_BOUND;
	s->src_len = sizeof(struct sockaddr_in);
	return 0;
}

struct socktype {
	uint8_t family;
	uint8_t type;
	uint8_t protocol;
	uint8_t info;
};

static struct socktype socktype[2] = {
	{ AF_INET, SOCK_STREAM, IPPROTO_TCP, 0 },
	{ 0, }
};

/* Set up protocol private fields in new socket */
void netproto_setup(struct socket *s)
{
}

/* Helper belongs in network.c ? */
int netproto_socket(void)
{
	register struct socktype *st = socktype;
	register struct socket *s;
	uint_fast8_t famok = 0;

	if (!m4_present) {
		udata.u_error = ENETDOWN;
		return 0;
	}
	s = netproto_create();
	if (s == NULL) {
		udata.u_error = ENOBUFS;
		return 0;
	}
	/* Now check if it's one we can do */
	while(st->family) {
		if (st->family == udata.u_net.args[1]) {
			famok = 1;
			if (st->type == udata.u_net.args[2] &&
				(st->protocol == 0 || udata.u_net.args[3] == 0 || udata.u_net.args[3] == st->protocol)) {
				net_setup(s);
				s->s_class = udata.u_net.args[2];
				s->s_state = SS_UNCONNECTED;
				s->s_type = st->info;
				udata.u_net.sock = s->s_num;
				return 0;
			}
		}
		st++;
	}
	if (famok)
		udata.u_error = EPROTONOSUPPORT;
	else
		udata.u_error = EAFNOSUPPORT;
	return 0;
}


/* TODO: the wiznet is very different in API here - when a connection
   completes incoming it takes over that socket and we need to create a new
   socket for the next one. That will need some kind of split socket/wiznet
   indexing or a way to update the host side mapping (which may be cleaner) */
int netproto_listen(struct socket *s)
{
	
}

int netproto_accept_complete(struct socket *s)
{
	return 0;
}

/* Start connecting to a remote host. We can't implement the UDP case correctly
   in just hardware. */
int netproto_begin_connect(struct socket *s)
{
	
}

/* Close down a socket - preferably politely */
int netproto_close(struct socket *s)
{
	
}

/* We must avoid racing an interrupt handler when maninpulating the flags */
static void set_iflags(struct socket *s, uint8_t flags)
{
	irqflags_t irq = di();
	s->s_iflags |= flags;
	irqrestore(irq);
}

int netproto_read(struct socket *s)
{
	
}


arg_t netproto_write(struct socket *s, struct ksockaddr *ka)
{
	
}

arg_t netproto_shutdown(struct socket *s, uint8_t flag)
{

}



static void netdev_reload(void)
{
	
}

arg_t netproto_ioctl(struct socket *s, int op, char *ifr_u /* in user space */)
{
	
}

void netdev_init(void)
{

}

/* We probably want a generic ipv4 helper layer for some of this */
int netproto_find_local(struct ksockaddr *ka)
{
	
}


static void inc_autoport(void)
{
	register uint16_t port = ntohs(autoport);
	port++;
	if (port == 32767)
		port = 5000;
	autoport = ntohs(port);
}

int netproto_autobind(struct socket *s)
{
	
}

int netproto_bind(struct socket *s)
{
	
}

#endif

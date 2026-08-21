#ifndef MMB_UDP_H
#define MMB_UDP_H
/*
 *	WEB UDP - the first network family, and deliberately the
 *	smallest (PLAN-web.md §11 stage 1): the reference is MMudp.c,
 *	whose whole model is one bound receive socket, a fresh socket
 *	per send, and two M-string buffers that MM.MESSAGE$ and
 *	MM.ADDRESS$ read back.
 *
 *	WEB UDP SERVER PORT n     bind the receive socket (the WebMite's
 *	                          saved OPTION UDP SERVER PORT, as a
 *	                          statement - PLAN-web.md §3.2)
 *	WEB UDP INTERRUPT sub     fire on a received datagram
 *	WEB UDP SEND ip$, port, msg$
 *
 *	Receive is a poll, like every MMBasic interrupt here: mmb_int.h
 *	calls mmg_udp_poll() under this file's include guard, decimated,
 *	from the slot where the WebMite checks its network one-shots
 *	(MM_Misc.c:10015-10029).  The socket is O_NDELAY so the poll
 *	never sleeps.
 *
 *	SEND takes a dotted quad or a hostname - the mmb_net.h resolver
 *	(stage 2) sits behind mmn_aton exactly as MMudp.c's DNS path
 *	sits behind ip4addr_aton.
 *
 *	Faithful details: a datagram longer than 255 bytes is truncated
 *	(a BASIC string is the destination), nulls inside are kept - the
 *	M-string length carries them - and the address string is the
 *	sender's IP alone, no port, exactly as addressbuff has it.
 */

#include "mmb_net.h"

/*	The handler pointer and its arming (mmi_udp_int) live in
 *	mmb_int.h under this file's guard - arming is that file's
 *	business (__mm_int_armed), the sprite pattern. */
static int mm_udp_fd = -1;		/* the bound receive socket */
static int mm_udp_rx;			/* one-shot, the WebMite's UDPreceive */
static long long mm_udp_next;		/* poll decimation deadline */
static char mm_udp_msg[256];		/* M-string: the last datagram */
static char mm_udp_addr[17];		/* M-string: its sender's IP */

MMG_FN void mmg_udp_port(MMINTEGER port)
{
	unsigned char sa[16];

	if (mm_udp_fd >= 0) {
		mmn_close(mm_udp_fd);
		mm_udp_fd = -1;
	}
	if (port < 1 || port > 65535)
		mm_error("Number out of bounds");
	mm_udp_fd = mmn_socket(MMN_AF_INET, MMN_SOCK_DGRAM, MMN_IPPROTO_UDP);
	if (mm_udp_fd < 0)
		mm_error("Failed to allocate UDP server pcb");
	mmn_sin(sa, 0, (int)port);	/* INADDR_ANY */
	if (mmn_bind(mm_udp_fd, sa, 16) < 0) {
		mmn_close(mm_udp_fd);
		mm_udp_fd = -1;
		mm_error("failed to bind UDP port");
	}
	mmn_ndelay(mm_udp_fd);
}

MMG_FN void mmg_udp_send(const char *ip, MMINTEGER port, const char *msg)
{
	unsigned char ip4[4], sa[16];
	int fd, n;

	if (!mmn_aton(ip, ip4)) {
		/* a name, then - MMudp.c resolves here too, with the
		   same 5 s bound its Timer4 gives DNS */
		n = mmn_resolve(ip, ip4, 5000);
		if (n < 0)
			mm_error("Failed to find UDP address");
		if (n == 0)
			mm_error("Failed to convert web address");
	}
	if (port < 1 || port > 65535)
		mm_error("Number out of bounds");
	/* A fresh socket per send, as MMudp.c makes a fresh pcb: the
	   source port is ephemeral and the receive socket is never
	   disturbed. */
	fd = mmn_socket(MMN_AF_INET, MMN_SOCK_DGRAM, MMN_IPPROTO_UDP);
	if (fd < 0)
		mm_error("Failed to allocate UDP send pcb");
	mmn_sin(sa, ip4, (int)port);
	n = mmn_sendto(fd, msg + 1, mm_slen(msg), 0, sa, 16);
	mmn_close(fd);
	if (n < 0)
		mm_error("Failed to send UDP packet!");
}

/*	Drain the queue, never sleep.  The kernel queues datagrams where
 *	the WebMite's callback overwrites one buffer per arrival, so
 *	"the buffer holds the MOST RECENT datagram" is the semantic to
 *	keep: read until empty and let the last one win.  Called from
 *	mm_int_poll and, so the buffers are live even in a program with
 *	no interrupt armed, from the MM.MESSAGE$/MM.ADDRESS$ readers. */
MMG_FN void mmg_udp_poll(void)
{
	unsigned char sa[16];
	int sl, n, i, got = 0;

	if (mm_udp_fd < 0)
		return;
	for (;;) {
		sl = 16;
		n = mmn_recvfrom(mm_udp_fd, mm_udp_msg + 1, 255, 0, sa, &sl);
		if (n <= 0)
			break;
		got = n;
	}
	if (!got)
		return;
	n = got;
	mm_udp_msg[0] = (char)n;
	i = 1;
	i += mmn_decb(mm_udp_addr + i, sa[4]);
	mm_udp_addr[i++] = '.';
	i += mmn_decb(mm_udp_addr + i, sa[5]);
	mm_udp_addr[i++] = '.';
	i += mmn_decb(mm_udp_addr + i, sa[6]);
	mm_udp_addr[i++] = '.';
	i += mmn_decb(mm_udp_addr + i, sa[7]);
	mm_udp_addr[0] = (char)(i - 1);
	mm_udp_rx = 1;
}

MMG_FN char *mm_udp_message(void)
{
	mmg_udp_poll();
	return mm_udp_msg;
}

MMG_FN char *mm_udp_address(void)
{
	mmg_udp_poll();
	return mm_udp_addr;
}

#endif /* MMB_UDP_H */

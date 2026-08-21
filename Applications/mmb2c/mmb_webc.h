#ifndef MMB_WEBC_H
#define MMB_WEBC_H
/*
 *	The WEB TCP/TLS client - PLAN-web.md §3.1, reference
 *	MMTCPclient.c.  One connection at a time, held in a plain fd:
 *
 *	WEB OPEN TCP CLIENT host$, port [,timeout]
 *	WEB OPEN TLS CLIENT host$, port [,timeout]   (stage 3 arms it)
 *	WEB TCP CLIENT REQUEST req$, a%() [,timeout]
 *	WEB TCP CLIENT READ a%() [,timeout]
 *	WEB TCP CLIENT WRITE ls%() [,timeout]
 *	WEB CLOSE TCP CLIENT
 *
 *	TLS is the two-line difference tlsget.c promised: IPPROTO_TLS on
 *	the socket, and SIOCTLSHOST with the hostname before connect -
 *	only when the host was typed as a name, which is the WebMite's
 *	own SNI rule (MMTCPclient.c:461).
 *
 *	Replicated semantics that took reading to find, kept because a
 *	program somewhere depends on each:
 *
 *	- REQUEST DISCARDS any unread input before writing.  The WebMite
 *	  drops data that arrives with no buffer armed
 *	  (tcp_client_recv:149-155), so between REQUESTs a server's
 *	  unsolicited output vanishes - and SMTP flows count on it: the
 *	  220 greeting must not come back as EHLO's answer.  READ does
 *	  NOT discard - consuming the greeting is what READ is for.
 *	- REQUEST/READ wait for the FIRST data up to the timeout, then
 *	  keep collecting for a further fixed 500 ms
 *	  (MMTCPclient.c:586-594) - one segment is rarely the whole
 *	  reply.
 *	- The reply buffer is the LONGSTRING shape: count in a(0), bytes
 *	  from a(1); what overflows the array is dropped, the count
 *	  stops at capacity.  Nulls in the payload are KEPT - the
 *	  null-to-space mangling is the server side's, not the client's.
 *	- No BASIC interrupts fire inside these waits.  MMBasic's wait
 *	  loops pump lwIP but never check_interrupt, so a bare deadline
 *	  loop here is the faithful shape, not an omission.
 *	- The WebMite's "Connecting to x port n" / "Connected" console
 *	  chatter is not reproduced (it grew OPTION SUPPRESSSTATUS for a
 *	  reason); the error texts are its own.
 *
 *	A connection refused costs the open timeout rather than failing
 *	fast: the libcall boundary carries no errno, so "still
 *	connecting" and "refused" both read as connect() != 0 until the
 *	deadline.  The WebMite's refused open ends in much the same
 *	timeout.  A peer RESET during a wait looks the same way; a peer
 *	CLOSE is seen properly (read 0 = EOF).
 */

#include "mmb_net.h"

static int mm_webc_fd = -1;
static signed char mm_webc_eof;

MMG_FN void mmg_webc_close(void)
{
	if (mm_webc_fd < 0)
		mm_error("No connection");
	mmn_close(mm_webc_fd);
	mm_webc_fd = -1;
}

MMG_FN void mmg_webc_open(const char *host, MMINTEGER port,
			  MMINTEGER tmo, int tls)
{
	unsigned char ip4[4], sa[16];
	unsigned long t0;
	int fd, r, dotted;

	if (port < 1 || port > 65535 || tmo < 1 || tmo > 100000)
		mm_error("Number out of bounds");
	if (mm_webc_fd >= 0) {
		/* a new OPEN replaces the old connection, silently -
		   close_tcpclient at MMTCPclient.c:292 */
		mmn_close(mm_webc_fd);
		mm_webc_fd = -1;
	}
	mm_webc_eof = 0;

	dotted = mmn_aton(host, ip4);
	if (!dotted) {
		r = mmn_resolve(host, ip4, (long)tmo);
		if (r < 0)
			mm_error("Failed to find TCP address");
		if (r == 0)
			mm_error("Failed to convert web address");
	}

	fd = mmn_socket(MMN_AF_INET, MMN_SOCK_STREAM,
			tls ? MMN_IPPROTO_TLS : MMN_IPPROTO_TCP);
	if (fd < 0)
		mm_error("failed to create pcb");
	if (tls && !dotted) {
		/* SNI, and what the certificate is checked against */
		char h[256];
		int n = mm_slen(host);

		memcpy(h, host + 1, n);
		h[n] = 0;
		mmn_ioctl(fd, MMN_SIOCTLSHOST, h);
	}
	mmn_ndelay(fd);
	mmn_sin(sa, ip4, (int)port);

	t0 = MMN_US();
	for (;;) {
		r = mmn_connect(fd, sa, 16);
		if (r == 0)
			break;
		if ((long)MMN_SINCE(t0) >= (long)tmo * 1000L) {
			mmn_close(fd);
			mm_error(tls ?
			  "No response from TLS server (handshake timeout)" :
			  "No response from client");
		}
		mm_pause(2.0);
	}
	mm_webc_fd = fd;
}

/*	Collect into the LONGSTRING buffer until the deadline; first_us
 *	bounds the wait for the FIRST byte, then the fixed 500 ms drain
 *	runs whatever happens.  Returns bytes received in total. */
MMG_FN long mmg_webc_collect(MMINTEGER *a, int cells, long tmo_ms)
{
	char *dst = (char *)&a[1];
	char sink[64];
	long cap = (long)(cells - 1) * 8;
	long got = 0;
	int n;
	unsigned long t0 = MMN_US();

	if (cap < 1)
		mm_error("array too small");
	a[0] = 0;
	/* phase one: the first data, or the timeout */
	for (;;) {
		n = mmn_read(mm_webc_fd, dst, (int)cap);
		if (n > 0)
			break;
		if (n == 0) {
			mm_webc_eof = 1;
			return 0;
		}
		if ((long)MMN_SINCE(t0) >= tmo_ms * 1000L)
			return 0;
		mm_pause(2.0);
	}
	got = n;
	a[0] = got;
	/* phase two: a fixed 500 ms of whatever else arrives.  A full
	   buffer keeps consuming and drops the excess, as the reference
	   copies buffer_left bytes and consumes the rest. */
	t0 = MMN_US();
	while ((long)MMN_SINCE(t0) < 500000L) {
		if (cap - got > 0)
			n = mmn_read(mm_webc_fd, dst + got,
				     (int)(cap - got));
		else
			n = mmn_read(mm_webc_fd, sink, (int)sizeof(sink));
		if (n > 0) {
			if (cap - got > 0) {
				got += n;
				a[0] = got;
			}
			continue;
		}
		if (n == 0) {
			mm_webc_eof = 1;
			break;
		}
		mm_pause(2.0);
	}
	return got;
}

/*	Drop whatever the server said since the last exchange - the
 *	no-buffer-armed drop, made explicit. */
MMG_FN void mmg_webc_drain(void)
{
	char sink[64];
	int n;

	for (;;) {
		n = mmn_read(mm_webc_fd, sink, (int)sizeof(sink));
		if (n <= 0) {
			if (n == 0)
				mm_webc_eof = 1;
			return;
		}
	}
}

MMG_FN void mmg_webc_request(const char *req, MMINTEGER *a, int cells,
			     MMINTEGER tmo)
{
	int len = mm_slen(req), sent = 0, n;
	unsigned long t0;

	if (mm_webc_fd < 0)
		mm_error("No connection");
	if (tmo < 1 || tmo > 100000)
		mm_error("Number out of bounds");
	mmg_webc_drain();
	t0 = MMN_US();
	while (sent < len) {
		n = mmn_write(mm_webc_fd, (char *)req + 1 + sent,
			      len - sent);
		if (n > 0) {
			sent += n;
			continue;
		}
		if ((long)MMN_SINCE(t0) >= (long)tmo * 1000L)
			mm_error("write failed");
		mm_pause(2.0);
	}
	if (mmg_webc_collect(a, cells, (long)tmo) == 0 && a[0] == 0)
		mm_error("No response from server");
}

MMG_FN void mmg_webc_read(MMINTEGER *a, int cells, MMINTEGER tmo)
{
	if (mm_webc_fd < 0)
		mm_error("No connection");
	if (tmo < 1 || tmo > 100000)
		mm_error("Number out of bounds");
	if (mmg_webc_collect(a, cells, (long)tmo) == 0 && a[0] == 0)
		mm_error("No response from server");
}

MMG_FN void mmg_webc_write(const MMINTEGER *a, MMINTEGER tmo)
{
	const char *src = (const char *)&a[1];
	long len = (long)(a[0] < 0 ? 0 : a[0]);
	long sent = 0;
	int n, chunk;
	unsigned long t0;

	if (mm_webc_fd < 0)
		mm_error("No connection");
	if (tmo < 1 || tmo > 100000)
		mm_error("Number out of bounds");
	t0 = MMN_US();
	while (sent < len) {
		chunk = (len - sent) > 1024 ? 1024 : (int)(len - sent);
		n = mmn_write(mm_webc_fd, (char *)src + sent, chunk);
		if (n > 0) {
			sent += n;
			continue;
		}
		if ((long)MMN_SINCE(t0) >= (long)tmo * 1000L)
			mm_error("Send timeout");
		mm_pause(2.0);
	}
}

#endif /* MMB_WEBC_H */

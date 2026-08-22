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
 *	errno crosses the libcall boundary as the neterr door (Fuzix
 *	numbering), because the polls need it: a refused connect fails
 *	fast instead of burning the timeout, and a reset - or the TLS
 *	close-notify racing a reply - ends a collect instead of reading
 *	as "still waiting" until the deadline, which is what it did
 *	until a server's 503-and-close proved it (PLAN-web.md stage-6
 *	notes).  A peer CLOSE is read 0 = EOF, as ever.
 */

#include "mmb_net.h"

static int mm_webc_fd = -1;
static signed char mm_webc_eof;

/*	The polling pause.  mm_pause under 100 ms SPINS (usleep's
 *	decisecond floor), and a spinning process starves the kernel's
 *	TLS receive: measured on the board, a hot O_NDELAY loop saw
 *	NOTHING for 30 s (6.5 M polls) where a blocking read got the
 *	reply - fast responses squeak through, slow ones never arrive.
 *	So: spin only through a short fast-response window, then REALLY
 *	sleep in decisecond steps, which hands the machine to the pump
 *	(and to every other process - 30 s of spin was hostile anyway).
 */
MMG_FN void mmw_poll_pause(unsigned long t0)
{
	if (MMN_SINCE(t0) < 250000UL)
		mm_pause(2.0);
	else
		mm_pause(100.0);
}

MMG_FN void mmg_webc_close(void)
{
	if (mm_webc_fd < 0) {
		mm_error("No connection");
		return;
	}
	mmn_close(mm_webc_fd);
	mm_webc_fd = -1;
}

/*	Every raise here cleans up FIRST and returns AFTER: a program
 *	with ON ERROR SKIP armed comes back from mm_error, and retic.bas
 *	wraps every one of these calls exactly that way.  A raise that
 *	fell through would loop on a dead fd for ever. */
MMG_FN void mmg_webc_open(const char *host, MMINTEGER port,
			  MMINTEGER tmo, int tls)
{
	unsigned char ip4[4], sa[16];
	unsigned long t0;
	int fd, r, dotted;

	if (port < 1 || port > 65535 || tmo < 1 || tmo > 100000) {
		mm_error("Number out of bounds");
		return;
	}
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
		if (r < 0) {
			mm_error("Failed to find TCP address");
			return;
		}
		if (r == 0) {
			mm_error("Failed to convert web address");
			return;
		}
	}

	fd = mmn_socket(MMN_AF_INET, MMN_SOCK_STREAM,
			tls ? MMN_IPPROTO_TLS : MMN_IPPROTO_TCP);
	if (fd < 0) {
		mm_error("failed to create pcb");
		return;
	}
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
		r = mmn_errno();
		if (r != MMN_EAGAIN && r != MMN_EALREADY &&
		    r != MMN_EINPROGRESS) {
			/* refused, reset, unreachable: fail NOW rather
			   than burning the timeout */
			mmn_close(fd);
			mm_error(tls ?
			  "No response from TLS server (handshake timeout)" :
			  "No response from client");
			return;
		}
		if ((long)MMN_SINCE(t0) >= (long)tmo * 1000L) {
			mmn_close(fd);
			mm_error(tls ?
			  "No response from TLS server (handshake timeout)" :
			  "No response from client");
			return;
		}
		mmw_poll_pause(t0);
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

	if (cap < 1) {
		mm_error("array too small");
		return 0;
	}
	a[0] = 0;
	/* phase one: the first data, or the timeout.  A read error
	   that is NOT EAGAIN is the connection dying (reset, or the
	   TLS close-notify racing the reply) - found the hard way:
	   without the errno check a reset read as "still waiting"
	   until the timeout. */
	for (;;) {
		n = mmn_read(mm_webc_fd, dst, (int)cap);
		if (n > 0)
			break;
		if (n == 0 || mmn_errno() != MMN_EAGAIN) {
			mm_webc_eof = 1;
			return 0;
		}
		if ((long)MMN_SINCE(t0) >= tmo_ms * 1000L)
			return 0;
		mmw_poll_pause(t0);
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
		if (n == 0 || mmn_errno() != MMN_EAGAIN) {
			mm_webc_eof = 1;
			break;
		}
		mmw_poll_pause(t0);
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
			if (n == 0 || mmn_errno() != MMN_EAGAIN)
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

	if (mm_webc_fd < 0) {
		mm_error("No connection");
		return;
	}
	if (tmo < 1 || tmo > 100000) {
		mm_error("Number out of bounds");
		return;
	}
	mmg_webc_drain();
	t0 = MMN_US();
	while (sent < len) {
		n = mmn_write(mm_webc_fd, (char *)req + 1 + sent,
			      len - sent);
		if (n > 0) {
			sent += n;
			continue;
		}
		if ((n < 0 && mmn_errno() != MMN_EAGAIN) ||
		    (long)MMN_SINCE(t0) >= (long)tmo * 1000L) {
			mm_error("write failed");
			return;
		}
		mmw_poll_pause(t0);
	}
	if (mmg_webc_collect(a, cells, (long)tmo) == 0 && a[0] == 0)
		mm_error("No response from server");
}

MMG_FN void mmg_webc_read(MMINTEGER *a, int cells, MMINTEGER tmo)
{
	if (mm_webc_fd < 0) {
		mm_error("No connection");
		return;
	}
	if (tmo < 1 || tmo > 100000) {
		mm_error("Number out of bounds");
		return;
	}
	if (mmg_webc_collect(a, cells, (long)tmo) == 0 && a[0] == 0)
		mm_error("No response from server");
}

/*
 *	WEB TLS CA file$ / WEB TLS NOVERIFY - tlsca(8)'s exact recipe:
 *	read the bundle, +1 NUL so PEM parses, hand the kernel a
 *	pointer through NETIOC_TLSCA on /dev/sys.  The kernel parses
 *	during the ioctl, so the buffer is freed on return.  This is
 *	MACHINE state, as it is for tlsca(8) - and effectively for the
 *	WebMite too, where the machine and the program are one.
 *	Until a bundle is loaded a TLS session is encrypted but NOT
 *	authenticated; run WEB NTP (or ntpdate) first, or every
 *	certificate on the internet looks expired.
 */
#define MMW_CA_MAX	20480	/* "keep the bundle small" - tlsca.c */

MMG_FN void mmg_webc_tlsdo(void *buf, unsigned long len)
{
	struct { void *buf; unsigned long len; } ca;
	int sys = mmn_open_rw("/dev/sys");

	if (sys < 0) {
		mm_error("cannot open /dev/sys");
		return;
	}
	ca.buf = buf;
	ca.len = len;
	if (mmn_ioctl(sys, MMN_NETIOC_TLSCA, &ca) < 0) {
		mmn_close(sys);
		mm_error("Failed to parse CA bundle");
		return;
	}
	mmn_close(sys);
}

MMG_FN void mmg_webc_tlsca(const char *fname)
{
	char path[256];
	char *buf;
	long got;
	int fd, n, fl = mm_slen(fname);

	if (fl == 0) {
		mm_error("Filename required");
		return;
	}
	memcpy(path, fname + 1, fl);
	path[fl] = 0;
	fd = mmn_open_ro(path);
	if (fd < 0) {
		mm_error("Cannot find file");
		return;
	}
	buf = (char *)mm_lheap(MMW_CA_MAX + 1);
	got = 0;
	while (got < MMW_CA_MAX) {
		n = mmn_read(fd, buf + got, (int)(MMW_CA_MAX - got));
		if (n <= 0)
			break;
		got += n;
	}
	mmn_close(fd);
	if (got == 0) {
		mm_lfree(buf);
		mm_error("Empty CA file");
		return;
	}
	if (got >= MMW_CA_MAX) {
		mm_lfree(buf);
		mm_error("CA file too big");
		return;
	}
	buf[got] = 0;		/* PEM must be NUL terminated */
	mmg_webc_tlsdo(buf, (unsigned long)(got + 1));
	mm_lfree(buf);
}

MMG_FN void mmg_webc_tlsnoverify(void)
{
	mmg_webc_tlsdo((void *)0, 0);
}

MMG_FN void mmg_webc_write(const MMINTEGER *a, MMINTEGER tmo)
{
	const char *src = (const char *)&a[1];
	long len = (long)(a[0] < 0 ? 0 : a[0]);
	long sent = 0;
	int n, chunk;
	unsigned long t0;

	if (mm_webc_fd < 0) {
		mm_error("No connection");
		return;
	}
	if (tmo < 1 || tmo > 100000) {
		mm_error("Number out of bounds");
		return;
	}
	t0 = MMN_US();
	while (sent < len) {
		chunk = (len - sent) > 1024 ? 1024 : (int)(len - sent);
		n = mmn_write(mm_webc_fd, (char *)src + sent, chunk);
		if (n > 0) {
			sent += n;
			continue;
		}
		if ((n < 0 && mmn_errno() != MMN_EAGAIN) ||
		    (long)MMN_SINCE(t0) >= (long)tmo * 1000L) {
			mm_error("Send timeout");
			return;
		}
		mmw_poll_pause(t0);
	}
}

#endif /* MMB_WEBC_H */

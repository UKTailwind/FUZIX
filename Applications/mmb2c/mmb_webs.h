#ifndef MMB_WEBS_H
#define MMB_WEBS_H
/*
 *	The WEB TCP server - PLAN-web.md §3.2, reference MMtcpserver.c.
 *	Eight connection slots behind one listener:
 *
 *	WEB TCP SERVER PORT n        open the listener (the WebMite's
 *	                             saved OPTION TCP SERVER PORT, as a
 *	                             statement - PLAN-web.md §3.2)
 *	WEB TCP INTERRUPT sub|0      fire when any slot receives
 *	WEB TCP READ n, a%()         the buffered request, consumed
 *	WEB TCP SEND n, a%()         raw payload to slot n
 *	WEB TCP CLOSE n              close slot n
 *	WEB TRANSMIT CODE n, code    a bare status line, then close
 *	WEB TRANSMIT FILE n, f$, m$  200 + Content-type + the file
 *	(WEB TRANSMIT PAGE is stage 5.)
 *
 *	Accept and receive are the poll: mmb_int.h calls mmg_webs_poll()
 *	under this file's guard, decimated, from the WebMite's own
 *	network-one-shot slot.  Faithful shapes, each one read out of
 *	MMtcpserver.c:
 *
 *	- Slots are 1..8 at the surface, MM.INFO(MAX CONNECTIONS) is 8.
 *	- A received request REPLACES the slot's buffer (the reference
 *	  frees and reallocs per lwIP segment, so a two-segment POST
 *	  kept only the tail; the kernel's coalescing here can only make
 *	  that better, never worse).
 *	- NULLS IN A REQUEST BECOME SPACES (MMtcpserver.c:212-214) - the
 *	  server side's mangling, observable and therefore kept.
 *	- READ with nothing pending ZEROES the array and returns; a
 *	  request longer than the array is "array too small", checked
 *	  against capacity minus the count cell, as the reference checks
 *	  size - 8 (MMtcpserver.c:842-848).
 *	- A slot idle past 5000 ms is reaped by the poll - the WebMite's
 *	  ServerResponceTime default, without the option.
 *	- TRANSMIT CODE is the reference's literal trick: the three
 *	  digits written over "404" in a canned HTTP/1.0 status line.
 *	- TRANSMIT FILE sends the reference's exact header
 *	  (Server:CPi, Connection:close), streams the file, 404s a
 *	  missing one, and closes either way.
 *	- Raises clean up first and return after - the ON ERROR SKIP
 *	  rule every net header follows.
 */

#include "mmb_net.h"

#define MMW_NSLOT	8
#define MMW_REQMAX	2048	/* per-slot request buffer */
#define MMW_IDLE_US	5000000L

static int mm_webs_lfd = -1;		/* the listener */
static int mm_webs_fd[MMW_NSLOT];	/* slot fds, -1 free */
static char *mm_webs_req[MMW_NSLOT];	/* mm_lheap'd on first use */
static short mm_webs_len[MMW_NSLOT];	/* bytes buffered */
static signed char mm_webs_trig[MMW_NSLOT];	/* the WebMite's inttrig */
static unsigned long mm_webs_seen[MMW_NSLOT];	/* last activity, MMN_US */
static int mm_webs_rx;			/* one-shot, the WebMite's TCPreceived */
static long long mm_webs_next;		/* poll decimation deadline */

MMG_FN void mmg_webs_port(MMINTEGER port)
{
	unsigned char sa[16];
	int i;

	if (mm_webs_lfd >= 0) {
		mmn_close(mm_webs_lfd);
		mm_webs_lfd = -1;
	}
	if (port < 1 || port > 65535) {
		mm_error("Number out of bounds");
		return;
	}
	for (i = 0; i < MMW_NSLOT; i++) {
		if (mm_webs_fd[i] > 0)
			mmn_close(mm_webs_fd[i]);
		mm_webs_fd[i] = -1;
		mm_webs_len[i] = 0;
		mm_webs_trig[i] = 0;
	}
	mm_webs_lfd = mmn_socket(MMN_AF_INET, MMN_SOCK_STREAM,
				 MMN_IPPROTO_TCP);
	if (mm_webs_lfd < 0) {
		mm_error("Failed to create TCP server");
		return;
	}
	mmn_sin(sa, 0, (int)port);	/* INADDR_ANY */
	if (mmn_bind(mm_webs_lfd, sa, 16) < 0 ||
	    mmn_listen(mm_webs_lfd, MMW_NSLOT) < 0) {
		mmn_close(mm_webs_lfd);
		mm_webs_lfd = -1;
		mm_error("failed to bind to port");
		return;
	}
	mmn_ndelay(mm_webs_lfd);
	/*	fd 0 is a real answer from accept() nowhere, but -1 is the
	 *	free marker and 0 never arrives (stdin exists), so > 0
	 *	guards above stay honest. */
}

MMG_FN void mmg_webs_slotclose(int i)
{
	if (mm_webs_fd[i] >= 0)
		mmn_close(mm_webs_fd[i]);
	mm_webs_fd[i] = -1;
	mm_webs_len[i] = 0;
	mm_webs_trig[i] = 0;
}

/*	One pass: reap idle slots, accept into free ones, read whatever
 *	waits.  Never sleeps - every fd is O_NDELAY. */
MMG_FN void mmg_webs_poll(void)
{
	int i, fd, n;

	if (mm_webs_lfd < 0)
		return;
	for (i = 0; i < MMW_NSLOT; i++) {
		if (mm_webs_fd[i] < 0)
			continue;
		if ((long)MMN_SINCE(mm_webs_seen[i]) > MMW_IDLE_US) {
			mmg_webs_slotclose(i);
			continue;
		}
		if (mm_webs_req[i] == 0)
			mm_webs_req[i] = (char *)mm_lheap(MMW_REQMAX);
		n = mmn_read(mm_webs_fd[i], mm_webs_req[i], MMW_REQMAX);
		if (n == 0) {		/* FIN - close, as the reference
					   closes on a NULL pbuf */
			mmg_webs_slotclose(i);
			continue;
		}
		if (n > 0) {
			int j;

			/* nulls to spaces - MMtcpserver.c:212-214 */
			for (j = 0; j < n; j++)
				if (mm_webs_req[i][j] == 0)
					mm_webs_req[i][j] = ' ';
			mm_webs_len[i] = (short)n;
			mm_webs_trig[i] = 1;
			mm_webs_seen[i] = MMN_US();
			mm_webs_rx = 1;
		}
	}
	for (;;) {
		fd = mmn_accept(mm_webs_lfd, 0, 0);
		if (fd < 0)
			break;
		for (i = 0; i < MMW_NSLOT; i++)
			if (mm_webs_fd[i] < 0)
				break;
		if (i == MMW_NSLOT) {
			/* the reference warns "No free connections" and
			   leaks the pcb; refusing outright is kinder */
			mmn_close(fd);
			break;
		}
		mmn_ndelay(fd);
		mm_webs_fd[i] = fd;
		mm_webs_len[i] = 0;
		mm_webs_trig[i] = 0;
		mm_webs_seen[i] = MMN_US();
	}
}

MMG_FN int mmg_webs_slot(MMINTEGER n)
{
	if (n < 1 || n > MMW_NSLOT) {
		mm_error("Number out of bounds");
		return -1;
	}
	return (int)n - 1;
}

MMG_FN void mmg_webs_read(MMINTEGER n, MMINTEGER *a, int cells)
{
	long cap = (long)cells * 8;
	int i = mmg_webs_slot(n);

	if (i < 0)
		return;
	/* the WebMite's ProcessWeb pumps constantly, interrupt or not -
	   so a reader in a plain loop must accept and receive too */
	mmg_webs_poll();
	if (!mm_webs_trig[i]) {
		/* nothing pending: zero the array, as the reference
		   memsets the whole destination */
		memset(a, 0, (size_t)cap);
		return;
	}
	if (cap - 8 < (long)mm_webs_len[i]) {
		mm_error("array too small");
		return;
	}
	memcpy(&a[1], mm_webs_req[i], (size_t)mm_webs_len[i]);
	a[0] = mm_webs_len[i];
	mm_webs_trig[i] = 0;
}

/*	The write engine TRANSMIT and SEND share: whole buffer, chunked,
 *	bounded by the reference's LWIP-send-timeout spirit (5 s). */
MMG_FN int mmg_webs_put(int i, const char *p, long len)
{
	unsigned long t0 = MMN_US();
	long sent = 0;
	int n, chunk;

	while (sent < len) {
		chunk = (len - sent) > 1024 ? 1024 : (int)(len - sent);
		n = mmn_write(mm_webs_fd[i], (char *)p + sent, chunk);
		if (n > 0) {
			sent += n;
			mm_webs_seen[i] = MMN_US();
			continue;
		}
		if ((long)MMN_SINCE(t0) > 5000000L)
			return -1;
		mm_pause(2.0);
	}
	return 0;
}

MMG_FN void mmg_webs_send(MMINTEGER n, const MMINTEGER *a)
{
	int i = mmg_webs_slot(n);
	long len;

	if (i < 0)
		return;
	if (mm_webs_fd[i] < 0) {
		mm_error("No connection");
		return;
	}
	len = (long)(a[0] < 0 ? 0 : a[0]);
	if (mmg_webs_put(i, (const char *)&a[1], len) < 0)
		mm_error("LWIP send data timeout");
}

MMG_FN void mmg_webs_close(MMINTEGER n)
{
	int i = mmg_webs_slot(n);

	if (i >= 0)
		mmg_webs_slotclose(i);
}

MMG_FN void mmg_webs_code(MMINTEGER n, MMINTEGER code)
{
	/* the reference's literal trick: the digits over "404" */
	char h[20];
	int i = mmg_webs_slot(n);

	if (i < 0)
		return;
	if (code < 100 || code > 999) {
		mm_error("Number out of bounds");
		return;
	}
	memcpy(h, "HTTP/1.0 404\r\n\r\n", 16);
	h[9] = (char)('0' + (code / 100));
	h[10] = (char)('0' + (code / 10) % 10);
	h[11] = (char)('0' + code % 10);
	if (mm_webs_fd[i] >= 0)
		mmg_webs_put(i, h, 16);
	mmg_webs_slotclose(i);
}

/*	TRANSMIT FILE's header, byte for byte the reference's
 *	(MMtcpserver.c:472-475), and the 404 for a file that is not
 *	there. */
MMG_FN void mmg_webs_file(MMINTEGER n, const char *fname, const char *mime)
{
	char hdr[256], path[256];
	char buf[512];
	long size, done;
	int i = mmg_webs_slot(n), fd, fl, hl, rd;

	if (i < 0)
		return;
	if (mm_webs_fd[i] < 0) {
		mm_error("No connection");
		return;
	}
	fl = mm_slen(fname);
	if (fl == 0) {
		mm_error("Cannot find file");
		return;
	}
	memcpy(path, fname + 1, fl);
	path[fl] = 0;
	size = (long)mm_filesize(fname);
	fd = size < 0 ? -1 : mmn_open_ro(path);
	if (fd < 0) {
		mmg_webs_put(i, "HTTP/1.0 404\r\n\r\n", 16);
		mmg_webs_slotclose(i);
		return;
	}
	hl = 0;
	memcpy(hdr, "HTTP/1.1 200 OK\r\nServer:CPi\r\nConnection:close\r\n"
	       "Content-type:", 60);
	hl = 60;
	memcpy(hdr + hl, mime + 1, mm_slen(mime));
	hl += mm_slen(mime);
	memcpy(hdr + hl, "\r\nContent-Length:", 17);
	hl += 17;
	if (size == 0)
		hdr[hl++] = '0';
	else {
		char d[12];
		int nd = 0;
		long v = size;

		while (v > 0 && nd < 12) {
			d[nd++] = (char)('0' + v % 10);
			v /= 10;
		}
		while (nd > 0)
			hdr[hl++] = d[--nd];
	}
	memcpy(hdr + hl, "\r\n\r\n", 4);
	hl += 4;
	if (mmg_webs_put(i, hdr, hl) < 0) {
		mmn_close(fd);
		mmg_webs_slotclose(i);
		mm_error("LWIP send data timeout");
		return;
	}
	done = 0;
	while (done < size) {
		rd = mmn_read(fd, buf, (int)sizeof(buf));
		if (rd <= 0)
			break;
		if (mmg_webs_put(i, buf, rd) < 0)
			break;
		done += rd;
	}
	mmn_close(fd);
	mmg_webs_slotclose(i);
}

/* ================= WEB TRANSMIT PAGE ================================
 *
 *	The engine half of PLAN-web.md §4's call-site substitution.  The
 *	translator pre-scans the page, compiles every {expression} inline
 *	at the statement - where the caller's locals and parameters are
 *	simply in scope - and emits a table of the expressions'
 *	NORMALISED texts.  At run time this engine streams the page,
 *	normalises each {...} the same way, and hands back its table
 *	index; the emitted switch evaluates and appends the value.
 *	Matching by text, not position, is what lets a page reorganised
 *	on the card keep working.
 *
 *	The reference algorithm is MMtcpserver.c:609-757: 0x1A dropped
 *	(xmodem padding), '{{' a literal '{', the expression ends at the
 *	FIRST '}' (string-blind, as the reference is), values formatted
 *	float / int / string exactly as there, two CRLF pairs inside the
 *	counted body (the reference's reversed bytes were a bug, fixed
 *	upstream), and the whole reply is one Content-Length'd 200.
 *
 *	Capacity: page size + bufsize (default 4096), the reference's
 *	own argument; what will not fit is dropped, which is kinder
 *	than the reference's hard stop at +256 (MMtcpserver.c:652).
 *	An expression the table does not know raises, naming it - the
 *	§4 draft choice, review-owned: the WebMite would render 0.
 */

struct mm_webpg {
	int slot;			/* -1: nothing to do (404'd/raised) */
	char *src;
	long slen, spos;
	char *body;
	long blen, bcap;
};

MMG_FN void mm_webpg_putn(struct mm_webpg *pg, const char *p, long n)
{
	long room = pg->bcap - pg->blen;

	if (n > room)
		n = room;
	if (n > 0) {
		memcpy(pg->body + pg->blen, p, (size_t)n);
		pg->blen += n;
	}
}

MMG_FN void mm_webpg_put_s(struct mm_webpg *pg, const char *m)
{
	mm_webpg_putn(pg, m + 1, mm_slen(m));
}

MMG_FN void mm_webpg_put_i(struct mm_webpg *pg, MMINTEGER v)
{
	char b[24];

	mm_int_to_str(b, v, 10);
	mm_webpg_putn(pg, b, (long)strlen(b));
}

MMG_FN void mm_webpg_put_f(struct mm_webpg *pg, MMFLOAT v)
{
	char b[40];

	mm_float_to_str(b, v, 0, MM_AUTO_PRECISION, ' ');
	mm_webpg_putn(pg, b, (long)strlen(b));
}

MMG_FN void mm_webpg_free(struct mm_webpg *pg)
{
	if (pg->src)
		mm_lfree(pg->src);
	if (pg->body)
		mm_lfree(pg->body);
	pg->src = pg->body = 0;
	pg->slot = -1;
}

MMG_FN void mm_webpg_start(struct mm_webpg *pg, MMINTEGER n,
			   const char *fname, MMINTEGER bufsize)
{
	char path[256];
	long size, got;
	int i = mmg_webs_slot(n), fd, fl, rd;

	pg->slot = -1;
	pg->src = pg->body = 0;
	pg->slen = pg->spos = pg->blen = 0;
	if (i < 0)
		return;			/* the raise was made */
	if (mm_webs_fd[i] < 0) {
		mm_error("No connection");
		return;
	}
	fl = mm_slen(fname);
	if (fl == 0) {
		mm_error("Cannot find file");
		return;
	}
	memcpy(path, fname + 1, fl);
	path[fl] = 0;
	size = (long)mm_filesize(fname);
	fd = size < 0 ? -1 : mmn_open_ro(path);
	if (fd < 0) {
		mmg_webs_put(i, "HTTP/1.0 404\r\n\r\n", 16);
		mmg_webs_slotclose(i);
		return;
	}
	if (bufsize < 0)
		bufsize = 0;
	pg->src = (char *)mm_lheap((unsigned long)size + 1);
	got = 0;
	while (got < size) {
		rd = mmn_read(fd, pg->src + got,
			      (int)(size - got > 4096 ? 4096 : size - got));
		if (rd <= 0)
			break;
		got += rd;
	}
	mmn_close(fd);
	pg->slen = got;
	pg->bcap = got + (long)bufsize + 8;	/* + the CRLF CRLF tail */
	pg->body = (char *)mm_lheap((unsigned long)pg->bcap);
	pg->slot = i;
}

MMG_FN int mm_webpg_next(struct mm_webpg *pg,
			 const char *const *tab, int ntab)
{
	char ex[160];
	static char msg[200];
	int xn, q, k;
	char c;

	if (pg->slot < 0)
		return -1;
	while (pg->spos < pg->slen) {
		c = pg->src[pg->spos++];
		if (c == 26)
			continue;
		if (c != '{') {
			mm_webpg_putn(pg, &c, 1);
			continue;
		}
		if (pg->spos < pg->slen && pg->src[pg->spos] == '{') {
			pg->spos++;
			c = '{';
			mm_webpg_putn(pg, &c, 1);
			continue;
		}
		/*	Collect to the first '}', normalised as the
		 *	translator normalised the table: verbatim inside a
		 *	"string literal", upcased with whitespace dropped
		 *	outside one. */
		xn = 0;
		q = 0;
		while (pg->spos < pg->slen) {
			c = pg->src[pg->spos++];
			if (c == '}')
				break;
			if (c == '"')
				q = !q;
			if (!q && (c == ' ' || c == '\t' ||
				   c == '\r' || c == '\n'))
				continue;
			if (!q && c >= 'a' && c <= 'z')
				c = (char)(c - 'a' + 'A');
			if (xn < (int)sizeof(ex) - 1)
				ex[xn++] = c;
		}
		ex[xn] = 0;
		for (k = 0; k < ntab; k++)
			if (strcmp(tab[k], ex) == 0)
				return k;
		memcpy(msg, "page expression not compiled in: {", 34);
		memcpy(msg + 34, ex, (size_t)xn);
		msg[34 + xn] = '}';
		msg[35 + xn] = 0;
		mm_webpg_free(pg);
		mm_error(msg);
		return -1;
	}
	/*	The end: the tail, once. */
	mm_webpg_putn(pg, "\r\n\r\n", 4);
	pg->spos = pg->slen + 1;
	return -1;
}

MMG_FN void mm_webpg_send(struct mm_webpg *pg)
{
	char hdr[96];
	int hl, nd;
	char d[12];
	long v;
	int i = pg->slot;

	if (i < 0) {
		mm_webpg_free(pg);
		return;
	}
	memcpy(hdr, "HTTP/1.1 200 OK\r\nServer:CPi\r\nConnection:close\r\n"
	       "Content-type:text/html\r\nContent-Length:", 86);
	hl = 86;
	v = pg->blen;
	nd = 0;
	if (v == 0)
		d[nd++] = '0';
	while (v > 0 && nd < 12) {
		d[nd++] = (char)('0' + v % 10);
		v /= 10;
	}
	while (nd > 0)
		hdr[hl++] = d[--nd];
	memcpy(hdr + hl, "\r\n\r\n", 4);
	hl += 4;
	if (mmg_webs_put(i, hdr, hl) == 0)
		mmg_webs_put(i, pg->body, pg->blen);
	mmg_webs_slotclose(i);
	mm_webpg_free(pg);
}

#endif /* MMB_WEBS_H */

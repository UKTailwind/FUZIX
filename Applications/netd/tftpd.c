/*
   tftpd - a trivial file transfer protocol server (RFC 1350)

	tftpd [-q] [-r] [-p port] [directory]

   Ported from MMBasic's, which is `net/MMtftp.c' hooked onto lwIP's
   `apps/tftp/tftp.c'.  MMBasic supplies only the five file callbacks;
   the protocol lives in lwIP.  Fuzix has no lwIP in userland - the
   stack is behind the socket layer - so the state machine had to come
   across with the callbacks, and this file is that state machine
   written against recvfrom/sendto.

   It is deliberately the SAME machine: one socket, bound to port 69,
   which is also the port every reply leaves from, one transfer at a
   time, a ten second timeout and five retries.  A client locks its
   transfer identifier onto whatever port the first DATA or ACK comes
   from, so replying from 69 is legal and is what every PicoMite has
   been doing; keeping it means one socket rather than two, which
   matters where the kernel has eight.

   Five things are deliberately NOT the same, and each is a fix rather
   than a preference:

   1.	The file is opened relative to the directory tftpd was started
	in, a leading `/' is dropped and a path containing `..' is
	refused - as httpd(1) next door does, and for the same reason:
	there is no chroot here and the whole disc is one namespace.
	MMBasic serves one SD card and has no such worry.

   2.	A filename may be 128 characters.  lwIP allows twenty, which is
	shorter than a Fuzix name plus the directory it is in.

   3.	An ACK for a block already sent is IGNORED.  lwIP answers it
	with ERROR "Wrong block number", which kills the transfer at
	the first lost packet: a client whose DATA n went missing
	re-sends ACK n-1, which is exactly this case.  Resending the
	data on a duplicate ACK is the other wrong answer - it is the
	Sorcerer's Apprentice, where one duplicate makes two - so RFC
	1123 4.2.3.1 says to do neither and let the timer retransmit.
	That is what happens here.

   4.	A write that runs out of card reports error 3, disk full,
	instead of error 2.  The client then says so.

   5.	-r refuses writes, and -q is `option suppress status'.

   netascii is not converted, as in MMBasic: both modes move the bytes
   through unchanged and the mode is only reported.  Use octet, which
   is what every client defaults to for anything that is not text.

   Options (RFC 2347: blksize, tsize, timeout) are ignored, which is
   what that RFC says a server that does not implement them must do -
   the client sees no OACK and falls back to plain 512-byte blocks.

   One wart is kept because the reference has it: nothing lingers after
   a transfer ends.  If the last ACK of a write is lost the client
   re-sends its last DATA, which arrives with no transfer in progress
   and is answered "No connection" - so the client reports a failure
   for a file that did in fact arrive whole.  A real BSD tftpd dallies
   a moment for exactly this; doing so here means keeping the peer and
   the last packet alive past the end of the transfer, which is more
   state than the one case has yet been seen to be worth.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <getopt.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define TFTP_RRQ	1
#define TFTP_WRQ	2
#define TFTP_DATA	3
#define TFTP_ACK	4
#define TFTP_ERROR	5

#define E_NOTDEF	0
#define E_NOTFOUND	1
#define E_ACCESS	2
#define E_FULL		3
#define E_ILLEGAL	4
#define E_BADTID	5

#define SEGSIZE		512
#define PKTSIZE		(SEGSIZE + 4)
#define MAXNAME		128

/*  lwIP's TFTP_TIMEOUT_MSECS and TFTP_MAX_RETRIES, in the units this
    program has.  Ten seconds is long for a LAN and is the number the
    reference has been shipping. */
#define TIMEOUT		10
#define RETRIES		5

static int sfd;
static struct sockaddr_in peer;		/* who we are talking to */
static char pkt[PKTSIZE];		/* what arrived */
static char out[PKTSIZE];		/* the last DATA or ACK we sent */
static int outlen;
static int quiet;
static int readonly;

/*  Set from the SIGALRM handler, which is the only thing that gets a
    blocking recvfrom() to return: this kernel has no select() and a
    socket read sleeps until a packet arrives or a signal lands. */
static volatile int timedout;

static void on_alarm(int sig)
{
    (void)sig;
    timedout = 1;
}

/*  Byte at a time, both ways.  The buffers are char arrays that a
    packet lands in at whatever offset the protocol says, so a u16 load
    through them would be unaligned - which on this machine is a fault,
    not a slow load. */
static void put16(char *p, unsigned v)
{
    p[0] = (char)(v >> 8);
    p[1] = (char)v;
}

static unsigned get16(const char *p)
{
    return ((unsigned)(unsigned char)p[0] << 8) | (unsigned char)p[1];
}

static int sendpkt(const struct sockaddr_in *to, const char *p, int n)
{
    for (;;) {
	if (sendto(sfd, p, n, 0, (const struct sockaddr *)to,
		   sizeof(*to)) >= 0)
	    return 0;
	if (errno == EINTR)
	    continue;
	perror("sendto");
	return -1;
    }
}

static void senderror(const struct sockaddr_in *to, int code, const char *msg)
{
    char e[PKTSIZE];
    int n = strlen(msg);

    if (n > SEGSIZE - 1)
	n = SEGSIZE - 1;
    put16(e, TFTP_ERROR);
    put16(e + 2, code);
    memcpy(e + 4, msg, n);
    e[4 + n] = 0;
    sendpkt(to, e, n + 5);
}

/*  Wait for the next packet of the transfer in progress.  Returns its
    length, 0 if nothing came within the timeout, -1 if the socket
    itself failed.

    A packet from anyone but our client is answered the way lwIP
    answers it and then ignored, which restarts the clock - a stranger
    talking to port 69 during a transfer can therefore hold it open.
    There is no authentication anywhere in TFTP, so that is not the
    weakest thing about leaving it running. */
static int waitpkt(void)
{
    struct sockaddr_in from;
    socklen_t fl;
    int n;

    for (;;) {
	timedout = 0;
	alarm(TIMEOUT);
	fl = sizeof(from);
	n = recvfrom(sfd, pkt, sizeof(pkt), 0,
		     (struct sockaddr *)&from, &fl);
	alarm(0);
	if (n < 0) {
	    if (timedout)
		return 0;
	    if (errno == EINTR)
		continue;
	    perror("recvfrom");
	    return -1;
	}
	if (from.sin_addr.s_addr != peer.sin_addr.s_addr ||
	    from.sin_port != peer.sin_port) {
	    senderror(&from, E_ACCESS,
		      "Only one connection at a time is supported");
	    if (!quiet) {
		printf("tftpd: %s refused, transfer in progress\n",
		       inet_ntoa(from.sin_addr));
		fflush(stdout);
	    }
	    continue;
	}
	if (n < 4)			/* a runt cannot be any opcode */
	    continue;
	return n;
    }
}

/*  An ERROR from the client.  MMBasic prints the text and abandons the
    file, which is all there is to do. */
static void peererror(int n)
{
    if (n > 4) {
	pkt[n - 1] = 0;			/* it need not be terminated */
	printf("TFTP Error: %s\n", pkt + 4);
    } else
	printf("TFTP Error: %u\n", get16(pkt + 2));
    fflush(stdout);
}

static void saytimeout(void)
{
    if (!quiet) {
	printf("tftpd: timed out, transfer abandoned\n");
	fflush(stdout);
    }
}

static void saydone(void)
{
    if (!quiet) {
	printf("TFTP transfer complete\n");
	fflush(stdout);
    }
}

/*  read() and write() on a file are allowed to be short.  A short read
    here would be read as the end of the transfer and truncate the file
    silently at the far end, which is the worst way for this to fail,
    so both are looped. */
static int fillread(int fd, char *p, int n)
{
    int got = 0, r;

    while (got < n) {
	r = read(fd, p + got, n - got);
	if (r < 0)
	    return -1;
	if (r == 0)
	    break;
	got += r;
    }
    return got;
}

static int fullwrite(int fd, const char *p, int n)
{
    int r;

    while (n > 0) {
	r = write(fd, p, n);
	if (r <= 0)
	    return -1;
	p += r;
	n -= r;
    }
    return 0;
}

/*  RRQ: we send the data and the client acknowledges each block. */
static void doread(const char *name)
{
    unsigned blk = 1;
    int in, n, tries, r;

    in = open(name, O_RDONLY);
    if (in < 0) {
	senderror(&peer, E_NOTFOUND, "Unable to open requested file.");
	return;
    }

    for (;;) {
	n = fillread(in, out + 4, SEGSIZE);
	if (n < 0) {
	    senderror(&peer, E_ACCESS,
		      "Error occurred while reading the file.");
	    break;
	}
	put16(out, TFTP_DATA);
	put16(out + 2, blk);
	outlen = n + 4;

	tries = 0;
	if (sendpkt(&peer, out, outlen) < 0)
	    break;
	for (;;) {
	    r = waitpkt();
	    if (r < 0)
		goto done;
	    if (r == 0) {
		if (++tries > RETRIES) {
		    saytimeout();
		    goto done;
		}
		if (sendpkt(&peer, out, outlen) < 0)
		    goto done;
		continue;
	    }
	    if (get16(pkt) == TFTP_ERROR) {
		peererror(r);
		goto done;
	    }
	    if (get16(pkt) != TFTP_ACK) {
		senderror(&peer, E_ILLEGAL, "Unknown operation");
		goto done;
	    }
	    if (get16(pkt + 2) == blk)
		break;
	    /*  An ACK for a block already behind us.  Neither answer it
		with an error nor resend on it: see the head of this
		file.  The retransmit above is the recovery. */
	}

	if (n < SEGSIZE) {		/* a short block ends the transfer */
	    saydone();
	    break;
	}
	blk = (blk + 1) & 0xFFFF;	/* wraps to 0 past 32M, as usual */
    }
  done:
    close(in);
}

/*  WRQ: the client sends the data and we acknowledge each block. */
static void dowrite(const char *name)
{
    unsigned blk = 1, b;
    int of, tries, r;

    if (readonly) {
	senderror(&peer, E_ACCESS, "Server is read-only");
	return;
    }
    of = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (of < 0) {
	senderror(&peer, E_NOTFOUND, "Unable to open requested file.");
	return;
    }

    put16(out, TFTP_ACK);		/* block 0 opens a write */
    put16(out + 2, 0);
    outlen = 4;
    if (sendpkt(&peer, out, outlen) < 0) {
	close(of);
	return;
    }

    tries = 0;
    for (;;) {
	r = waitpkt();
	if (r < 0)
	    break;
	if (r == 0) {
	    if (++tries > RETRIES) {
		saytimeout();
		break;
	    }
	    if (sendpkt(&peer, out, outlen) < 0)
		break;
	    continue;
	}
	if (get16(pkt) == TFTP_ERROR) {
	    peererror(r);
	    break;
	}
	if (get16(pkt) != TFTP_DATA) {
	    senderror(&peer, E_ILLEGAL, "Unknown operation");
	    break;
	}
	b = get16(pkt + 2);
	if (b == blk) {
	    if (fullwrite(of, pkt + 4, r - 4) < 0) {
		senderror(&peer,
			  errno == ENOSPC ? E_FULL : E_ACCESS,
			  errno == ENOSPC ? "Disk full"
					  : "error writing file");
		break;
	    }
	    put16(out, TFTP_ACK);
	    put16(out + 2, blk);
	    outlen = 4;
	    if (sendpkt(&peer, out, outlen) < 0)
		break;
	    tries = 0;
	    if (r - 4 < SEGSIZE) {	/* a short block ends the transfer */
		saydone();
		break;
	    }
	    blk = (blk + 1) & 0xFFFF;
	} else if (((b + 1) & 0xFFFF) == blk) {
	    /*  The block before this one, again: our ACK for it was
		lost.  Acknowledging a duplicate DATA is required of a
		receiver and starts nothing running away - it is
		resending DATA on a duplicate ACK that does that. */
	    char a[4];

	    put16(a, TFTP_ACK);
	    put16(a + 2, b);
	    if (sendpkt(&peer, a, 4) < 0)
		break;
	} else {
	    senderror(&peer, E_BADTID, "Wrong block number");
	    break;
	}
    }
    close(of);
}

/*  Relative to the directory we were started in, and nowhere else. */
static char *safepath(char *name)
{
    while (*name == '/')
	name++;
    if (!*name)
	return NULL;
    if (strstr(name, ".."))
	return NULL;
    return name;
}

/*  A request has arrived on port 69.  Parse it and run the whole
    transfer before looking at the socket again. */
static void request(int op, int n)
{
    char *name, *mode, *p, *end;
    int wr = (op == TFTP_WRQ);

    end = pkt + n;
    name = pkt + 2;
    for (p = name; p < end && *p; p++)
	;
    if (p >= end || p - name > MAXNAME) {
	senderror(&peer, E_ACCESS,
		  "Filename too long/not NULL terminated");
	return;
    }
    mode = p + 1;
    for (p = mode; p < end && *p; p++)
	;
    if (p >= end || p - mode > 16) {
	senderror(&peer, E_ACCESS, "Mode too long/not NULL terminated");
	return;
    }

    if (!quiet) {
	/*  MMBasic's wording, so that the same transfer reads the same
	    on both machines. */
	printf("TFTP request to %s %s file : %s\n",
	       wr ? "create" : "read",
	       strcmp(mode, "octet") == 0 ? "binary" : "ascii", name);
	fflush(stdout);
    }

    name = safepath(name);
    if (name == NULL) {
	senderror(&peer, E_ACCESS, "Illegal file name");
	return;
    }

    if (wr)
	dowrite(name);
    else
	doread(name);
}

static void usage(void)
{
    fprintf(stderr, "usage: tftpd [-q] [-r] [-p port] [directory]\n");
    exit(1);
}

int main(int argc, char *argv[])
{
    struct sockaddr_in addr;
    struct sockaddr_in from;
    socklen_t fl;
    int port = 69;
    int n, c;

    while ((c = getopt(argc, argv, "p:qr")) > 0) {
	switch (c) {
	case 'p':
	    port = atoi(optarg);
	    break;
	case 'q':
	    quiet = 1;
	    break;
	case 'r':
	    readonly = 1;
	    break;
	default:
	    usage();
	}
    }
    if (argv[optind] && chdir(argv[optind])) {
	perror(argv[optind]);
	exit(1);
    }

    signal(SIGALRM, on_alarm);

    sfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sfd < 0) {
	perror("af_inet sock_dgram 0");
	exit(1);
    }
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);	/* network order, or it is port 0x4500 */
    addr.sin_addr.s_addr = INADDR_ANY;
    memset(addr.sin_zero, 0, sizeof(addr.sin_zero));
    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr))) {
	/*  Port 69 is privileged, which is the usual reason. */
	perror("bind");
	exit(1);
    }

    if (!quiet) {
	printf("tftpd: serving this directory on port %d%s\n", port,
	       readonly ? ", read only" : "");
	fflush(stdout);
    }

    for (;;) {
	alarm(0);			/* no clock while nothing is going on */
	fl = sizeof(from);
	n = recvfrom(sfd, pkt, sizeof(pkt), 0,
		     (struct sockaddr *)&from, &fl);
	if (n < 0) {
	    if (errno == EINTR)
		continue;
	    perror("recvfrom");
	    return 1;
	}
	if (n < 4)
	    continue;
	peer = from;
	switch (get16(pkt)) {
	case TFTP_RRQ:
	case TFTP_WRQ:
	    request(get16(pkt), n);
	    break;
	case TFTP_DATA:
	case TFTP_ACK:
	    /*  Left over from a transfer that has already finished, or
		a client that never sent a request. */
	    senderror(&from, E_ACCESS, "No connection");
	    break;
	case TFTP_ERROR:
	    break;			/* nothing of ours to abandon */
	default:
	    senderror(&from, E_ILLEGAL, "Unknown operation");
	    break;
	}
    }
}

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "netdb.h"


static int sock;
static char buf[512];
static char *bufend = buf;
static char *readp = buf;
static int buflen = 0;

void writes(int fd, const char *p)
{
    write(fd, p, strlen(p));
}

void xflush(void)
{
    if (write(sock, buf, buflen) != buflen) {
        perror("write");
        exit(1);
    }
    buflen = 0;
}

void xwrites(const char *p)
{
    int l = strlen(p);
    if (l + buflen > 512)
        xflush();
    memcpy(buf + buflen, p, l);
    buflen += l;
}

int xread(void)
{
    int len;

    /* The first call we return the stray bytes from the line parser */
    if (bufend != buf && bufend > readp) {
        len = bufend - readp;
        memcpy(buf, readp, bufend - readp);
        bufend = buf;
        readp = buf;
        return len;
    }
    len = read(sock, buf, 512);
    if (len < 0) {
        perror("read");
        exit(1);
    }
    return len;
}

/*
 *	The body, and the chunked encoding it may arrive in.
 *
 *	This asks for HTTP/1.1, which is an invitation to chunk, and the
 *	code used to copy whatever came down the socket straight into the
 *	file.  So a chunked reply landed on disk complete with its
 *	framing: example.com saved as "22f", the body, and a "0" - the
 *	hex length of the one chunk and the end marker, written out as if
 *	they were content.
 *
 *	One buffer for the whole body, seeded with whatever the header
 *	parser had already pulled off the socket, so the two do not have
 *	to share xread()'s hand-back.
 */
static char bbuf[512];
static int blen, bpos;

static void body_init(void)
{
    blen = bpos = 0;
    if (bufend > readp) {
        blen = bufend - readp;
        memcpy(bbuf, readp, blen);
    }
    readp = bufend = buf;
}

/* True while there is a byte to be had; refills when empty. */
static int body_fill(void)
{
    int n;

    if (bpos < blen)
        return 1;
    n = read(sock, bbuf, sizeof(bbuf));
    if (n < 0) {
        perror("read");
        exit(1);
    }
    blen = n;
    bpos = 0;
    return n > 0;
}

static int body_getc(void)
{
    if (!body_fill())
        return -1;
    return (unsigned char)bbuf[bpos++];
}

/* A CRLF line from the body stream: the chunk header, or the empty
   line that follows a chunk's data. */
static int body_line(char *p, int max)
{
    int c, n = 0;

    for (;;) {
        c = body_getc();
        if (c == -1)
            return -1;
        if (c == '\n')
            break;
        if (c != '\r' && n < max - 1)
            p[n++] = c;
    }
    p[n] = 0;
    return n;
}

static void body_out(int of, int len)
{
    if (write(of, bbuf + bpos, len) != len) {
        perror("write");
        exit(1);
    }
    bpos += len;
}

static void copy_plain(int of)
{
    while (body_fill()) {
        body_out(of, blen - bpos);
        write(1, ".", 1);
    }
}

static void copy_chunked(int of)
{
    char line[64];
    long n;
    int avail;

    for (;;) {
        if (body_line(line, sizeof(line)) < 0)
            return;                     /* truncated */
        /* "1a2b" or "1a2b;ext=value" - strtol stops at the ';' */
        n = strtol(line, NULL, 16);
        if (n <= 0)
            return;                     /* 0: last chunk, trailers ignored */
        while (n > 0) {
            if (!body_fill())
                return;                 /* truncated */
            avail = blen - bpos;
            if (avail > n)
                avail = (int)n;
            body_out(of, avail);
            n -= avail;
        }
        body_line(line, sizeof(line));  /* the CRLF after the data */
        write(1, ".", 1);
    }
}

/* "Transfer-Encoding: chunked", case-insensitively, value anywhere in
   the field - it may be a list, and "chunked" is always last. */
static int hdr_chunked(const char *l)
{
    static const char te[] = "transfer-encoding:";
    static const char ch[] = "chunked";
    int i;

    for (i = 0; te[i]; i++)
        if (tolower((unsigned char)l[i]) != te[i])
            return 0;
    for (; l[i]; i++) {
        int j;
        for (j = 0; ch[j]; j++)
            if (tolower((unsigned char)l[i + j]) != ch[j])
                break;
        if (ch[j] == 0)
            return 1;
    }
    return 0;
}

int xreadline(void)
{
    int len;

    if (readp != buf && bufend > readp) {
        memcpy(buf, readp, bufend - readp);
        bufend -= (readp - buf);
    }
    readp = buf;

    len = read(sock, buf + buflen, 512 - buflen);
    if (len < 0) {
        perror("read");
        exit(1);
    }
    buflen += len;
    bufend += len;

    while(readp < bufend) {
        if (*readp == '\r' && readp[1] == '\n') {
            *readp++ = '\n';
            *readp++ = 0;
            len = readp - buf;
            buflen -= len;
            return len;
        }
        readp++;
    }
    writes(2,"htget: overlong/misformatted header\n");
    exit(1);
}
    

int main(int argc, char *argv[])
{
    struct sockaddr_in sin;
    struct hostent *h;
    uint16_t port = 80;
    char *pp;
    char *fp;
    int of;
    int code;
    int chunked = 0;
    uint8_t looped = 0;

    if (argc != 3) {
        writes(2, "htget url file\n");
        exit(1);
    }
    if (strncmp(argv[1], "http://", 7)) {
        writes(2, "htget: only http:// is supported.\n");
        exit(2);
    }
    argv[1] += 7;

    fp = strchr(argv[1], '/');
    if (fp)
        *fp++ = 0;

    pp = strrchr(argv[1], ':');
    if (pp) {
        *pp++ = 0;
        port = atoi(pp);
        if (port == 0) {
            writes(2, "htget: invalid port\n");
            exit(1);
        }
    }


    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    
    h = gethostbyname(argv[1]);
    if (h == NULL) {
        writes(2, "htget: unknown host \"");
        writes(2, argv[1]);
        writes(2, "\"\n");
        exit(1);
    }
    memcpy( &sin.sin_addr.s_addr, h->h_addr_list[0], 4 );

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket");
        exit(1);
    }
    if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("connect");
        exit(1);
    }

    xwrites("GET /");
    if (fp)
        xwrites(fp);
    xwrites(" HTTP/1.1\r\n");
    xwrites("Host: ");
    xwrites(argv[1]);
    if (pp) {
        xwrites(":");
        xwrites(pp);
    }
    xwrites("\r\nUser-Agent: Fuzix-htget/0.1\r\nConnection: close\r\n\r\n");
    xflush();

    do {
        xreadline();
        errno = 0;
        pp = strchr(buf, ' ');
        if (pp == NULL) {
            writes(2, "htget: invalid reply\n");
            writes(2, buf);
            exit(1);
        }
        pp++;
        code = strtoul(pp, &pp, 10);
        if (code < 100 || *pp++ != ' ') {
            writes(2, "htget: invalid reply\n");
            writes(2, buf);
            exit(1);
        }

        if (code  != 200)
            writes(2, buf);
        do {
            xreadline();
            if (hdr_chunked(buf))
                chunked = 1;
            if (code != 200)
                writes(2, buf);
        } while(*buf != '\n');

        /* A 100 code means "I'm thinking please wait then a header cycle then
           a real header and has a blank line following */
        if (code == 100)
            xreadline();
    } while (code == 100 && !looped++);

    of = open(argv[2], O_WRONLY|O_CREAT, 0666);
    if (of == -1) {
        perror(argv[2]);
        exit(1);
    }
    if (code == 200) {
        body_init();
        if (chunked)
            copy_chunked(of);
        else
            copy_plain(of);
    }
    write(1,"\n",1);
    close(of);
    close(sock);
    return 0;
}

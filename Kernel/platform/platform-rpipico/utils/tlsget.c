/*
 * tlsget - fetch a page over TLS, to prove the socket does what it says.
 *
 *	tlsget <ip> <hostname> [path] [port]
 *
 * The address and the name are given separately on purpose: the name
 * is what TLS needs (SNI, and the certificate check), and keeping the
 * resolver out of it keeps this a test of the socket and nothing else.
 *
 * The whole of the TLS-specific API is two lines: ask for IPPROTO_TLS
 * instead of plain TCP, and tell the socket the host name before
 * connecting.  Everything after that is connect/write/read/close on an
 * ordinary file descriptor - which is the point, and what lets mmbc
 * emit the same calls for WEB OPEN TCP CLIENT and WEB OPEN TLS CLIENT.
 *
 * NOTE: with no CA bundle loaded the session is encrypted but NOT
 * authenticated - anything in the path can present its own
 * certificate.  See NETIOC_TLSCA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>


/* These belong in <netinet/in.h> and <sys/ioctl.h> once TLS is more
   than an experiment; they are the kernel's Kernel/include/net_lwip.h
   values. */
#define IPPROTO_TLS 254
#define SIOCTLSHOST 0x0420

int main(int argc, char *argv[])
{
    struct sockaddr_in a;
    const char *ip;
    char req[256];
    char buf[512];
    const char *host, *path;
    int fd, n, total = 0, port;

    if (argc < 2) {
        /* The IP comes first and it really is an IP: there is no
           resolver in here on purpose (see the top of the file), and
           this line once said "host", which sent a whole debugging
           session to 255.255.255.255. */
        fprintf(stderr, "usage: tlsget ip [hostname] [path] [port]\n");
        return 1;
    }
    ip = argv[1];
    host = argc > 2 ? argv[2] : argv[1];
    path = argc > 3 ? argv[3] : "/";
    port = argc > 4 ? atoi(argv[4]) : 443;

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TLS);
    if (fd < 0) {
        perror("socket");
        return 1;
    }
    /* Before connect: this is the SNI name and, once a CA is loaded,
       the name the certificate has to match. */
    if (ioctl(fd, SIOCTLSHOST, (char *)host) < 0) {
        perror("SIOCTLSHOST");
        return 1;
    }

    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = inet_addr(ip);

    printf("connecting (handshake)...\n");
    fflush(stdout);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
        perror("connect");
        return 1;
    }
    printf("connected\n");

    sprintf(req, "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",
            path, host);
    n = strlen(req);
    if (write(fd, req, n) != n) {
        perror("write");
        return 1;
    }

    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        if (total == 0) {               /* show the status line only */
            buf[n] = 0;
            printf("%.40s\n", buf);
        }
        total += n;
    }
    if (n < 0)
        perror("read");
    printf("%d bytes\n", total);
    close(fd);
    return 0;
}

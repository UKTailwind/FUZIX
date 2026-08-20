/*
 * sockwait - block in read() on a UDP socket and do nothing else.
 *
 * A test instrument, not a tool.  ping(1) survives SIGTERM while
 * blocked in a socket read, and ping is not a clean specimen: it has
 * SIGALRM and SIGINT handlers and an alarm outstanding, so it is
 * impossible to tell whether the signal machinery or the socket sleep
 * is at fault.  This has no handlers, no alarm and no timeout - if it
 * survives a kill, the socket sleep is what is swallowing the signal.
 *
 *	sockwait [port]		default 7777
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

static void caught(int sig)
{
    /* Deliberately not exit(): saying so is the whole experiment. */
    write(1, "caught\n", 7);
}

int main(int argc, char *argv[])
{
    struct sockaddr_in a;
    char buf[64];
    int fd, n;

    /* With a handler installed, SIGTERM takes chksigset's "caught"
       branch instead of its default-action branch.  If this prints,
       the signal reaches the process while it is blocked in the socket
       read and only the default action is being lost. */
    if (argc > 2)
        signal(SIGTERM, caught);

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }
    /* CONNECT, not bind.  network.c's net_read refuses a read() on a
       socket below SS_CONNECTED - "read returned 0" is what a bound
       but unconnected datagram socket gives - so a connected one is
       the only shape that blocks, and it is the shape ping uses. The
       peer need not exist; nothing is ever sent to it. */
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = inet_addr(argc > 1 ? argv[1] : "192.168.1.254");
    a.sin_port = htons(9);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
        perror("connect");
        return 1;
    }
    printf("blocking in read\n");
    fflush(stdout);
    n = read(fd, buf, sizeof(buf));
    printf("read returned %d\n", n);
    return 0;
}

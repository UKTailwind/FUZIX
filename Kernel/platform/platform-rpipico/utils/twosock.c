/*
   twosock - hold two sockets open at once, then close them.

   The whole of the "tftpd makes the system unstable" reproduction,
   with the tftpd taken out.  What matters is not TFTP: it is that a
   SECOND socket exists while a first one is still open.

   netproto_create() scans the socket table from 0, so a program that
   opens one socket and exits is always slot 0 - which is every network
   program this machine had until tftpd: ping, dig, ntpdate, htget,
   tlsget.  A resident server pins slot 0, and the next program to open
   a socket gets slot 1.

   That matters because make_socket() stores the slot number in
   IN2SOCK(ino), which is c_node.i_addr[0] - the first direct block
   pointer of a real on-disk inode - and i_deref() skips f_trunc() for
   F_SOCK, so the inode is written back and freed with that pointer
   still in it.  blk_free() ignores block 0, so slot 0 is harmless and
   slot 1 is not.

   Run this with the radio up, then sync and run inoscan: a free inode
   should have appeared carrying i_addr[0] = 1.

   Read-only as far as anything you can see goes - it opens two
   sockets, closes them and exits.  The damage it demonstrates is done
   by the kernel, not by this program.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
/* after netinet/in.h and sys/socket.h: it uses in_addr_t and socklen_t
   and includes neither */
#include <arpa/inet.h>

int main(int argc, char *argv[])
{
    int n = (argc > 1) ? atoi(argv[1]) : 2;
    int fd[8];
    int i;

    if (n < 1 || n > 8)
	n = 2;

    for (i = 0; i < n; i++) {
	fd[i] = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd[i] < 0) {
	    perror("socket");
	    printf("opened %d of %d - is the radio up?\n", i, n);
	    while (--i >= 0)
		close(fd[i]);
	    return 1;
	}
	printf("socket %d -> fd %d\n", i, fd[i]);
    }

    /*  With a server IP, do a real round trip on the HIGHEST socket -
	the one whose slot is not 0.  IN2SOCK is what maps an inode back
	to a socket for read, write, close and ioctl, so a non-zero slot
	carrying data is the case that proves the mapping: if it were
	wrong, this would be answered on socket 0 or not at all.  A
	minimal DNS query for example.com, which any LAN resolver
	answers. */
    if (argc > 2) {
	static const unsigned char q[] = {
	    0x12, 0x34,			/* id */
	    0x01, 0x00,			/* recursion desired */
	    0x00, 0x01, 0x00, 0x00,	/* 1 question */
	    0x00, 0x00, 0x00, 0x00,
	    7, 'e','x','a','m','p','l','e', 3, 'c','o','m', 0,
	    0x00, 0x01, 0x00, 0x01	/* A, IN */
	};
	struct sockaddr_in to;
	unsigned char rbuf[64];
	int top = n - 1, r;

	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_port = htons(53);
	if (!inet_aton(argv[2], &to.sin_addr)) {
	    printf("bad server address %s\n", argv[2]);
	} else if (connect(fd[top], (struct sockaddr *)&to, sizeof(to)) < 0) {
	    perror("connect");
	} else if (write(fd[top], q, sizeof(q)) < 0) {
	    perror("write");
	} else {
	    alarm(5);
	    r = read(fd[top], rbuf, sizeof(rbuf));
	    alarm(0);
	    if (r < 0)
		perror("read");
	    else if (r >= 4 && rbuf[0] == 0x12 && rbuf[1] == 0x34)
		printf("slot %d: DNS round trip OK, %d bytes, id matches\n",
		       top, r);
	    else
		printf("slot %d: %d bytes, but not our reply\n", top, r);
	}
    }

    /* Close the highest slot LAST-to-first, so the interesting one -
       any slot above 0 - goes through i_deref while we watch. */
    for (i = n - 1; i >= 0; i--)
	close(fd[i]);

    printf("%d sockets opened and closed\n", n);
    return 0;
}

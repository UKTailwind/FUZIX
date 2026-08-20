/*
 * nbtest - does O_NDELAY actually work on a socket?
 *
 * Checks the claim the C manual makes: that an operation which would
 * have slept returns -1/EAGAIN instead.  Listens on a port nobody is
 * connecting to and accepts; blocking, that hangs for ever.
 *
 * Not installed on the card - build and uusend it when the claim needs
 * re-checking.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int argc, char *argv[])
{
	struct sockaddr_in a;
	int lfd, fd, i;

	lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd < 0) {
		perror("socket");
		return 1;
	}
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons(argc > 1 ? atoi(argv[1]) : 9099);
	if (bind(lfd, (struct sockaddr *)&a, sizeof(a)) < 0) {
		perror("bind");
		return 1;
	}
	if (listen(lfd, 1) < 0) {
		perror("listen");
		return 1;
	}
	if (fcntl(lfd, F_SETFL, O_NDELAY) < 0) {
		perror("fcntl");
		return 1;
	}
	printf("listening, non-blocking; accept x3 with nobody calling\n");
	for (i = 0; i < 3; i++) {
		errno = 0;
		fd = accept(lfd, NULL, NULL);
		printf("  accept -> %d, errno %d (%s)\n", fd, errno,
		       fd < 0 ? strerror(errno) : "connected");
		if (fd >= 0)
			close(fd);
		sleep(1);
	}
	printf("if those returned promptly, O_NDELAY works\n");
	close(lfd);
	return 0;
}

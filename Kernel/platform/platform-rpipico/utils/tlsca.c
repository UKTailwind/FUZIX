/*
 * tlsca - load the machine's CA bundle, so TLS actually authenticates.
 *
 *	tlsca /etc/ssl/ca.pem		load, and verify from now on
 *	tlsca -n			forget it, back to no verification
 *
 * WITHOUT THIS, A TLS SESSION IS ENCRYPTED BUT NOT AUTHENTICATED.  The
 * traffic cannot be read in the middle, but anything in the path can
 * present a certificate of its own and be believed, which is most of
 * what a certificate is for.  It is worth saying plainly because the
 * failure is invisible: an unauthenticated session fetches pages just
 * as happily as an authenticated one.
 *
 * SET THE CLOCK FIRST.  Verification checks the notBefore/notAfter
 * dates, so a board that has not run ntpdate since power-on is either
 * in 1970 or wherever the DS3231 left it, and every certificate on the
 * internet will look invalid.  MMBasic says the same thing about
 * running WEB NTP before WEB TLS CA.
 *
 * KEEP THE BUNDLE SMALL.  Every certificate in it is parsed into
 * mbedtls state, out of the lwIP heap that the packet buffers also
 * come from.  Three to five roots - five to ten kilobytes - is the
 * useful range; the full Mozilla set is about 200K and will not parse.
 * Pick the roots the sites you use are actually signed by.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#define NETIOC_TLSCA	0x0043		/* pico_ioctl.h is the authority */

struct net_ca {
	void *buf;
	unsigned long len;
};

static int netfd(void)
{
	int fd = open("/dev/sys", O_RDWR);

	if (fd < 0) {
		perror("/dev/sys");
		exit(1);
	}
	return fd;
}

int main(int argc, char *argv[])
{
	struct net_ca ca;
	struct stat st;
	char *buf;
	int fd, sys, n, got = 0;

	if (argc != 2) {
		fprintf(stderr, "usage: tlsca file | tlsca -n\n");
		return 1;
	}

	if (!strcmp(argv[1], "-n")) {
		ca.buf = NULL;
		ca.len = 0;
		sys = netfd();
		if (ioctl(sys, NETIOC_TLSCA, (char *)&ca) < 0) {
			perror("NETIOC_TLSCA");
			return 1;
		}
		printf("TLS: certificates are NOT checked\n");
		return 0;
	}

	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		perror(argv[1]);
		return 1;
	}
	if (fstat(fd, &st) < 0) {
		perror("fstat");
		return 1;
	}
	/*
	 * One byte more than the file, holding a NUL.  This is not
	 * tidiness: mbedtls decides PEM versus DER by looking for the
	 * terminator, and the length it is given must INCLUDE it.  A
	 * bundle passed without it is treated as DER, fails to parse,
	 * and the whole load is rejected.
	 */
	buf = malloc((size_t)st.st_size + 1);
	if (buf == NULL) {
		fprintf(stderr, "tlsca: %ld bytes will not fit\n",
			(long)st.st_size);
		return 1;
	}
	while ((n = read(fd, buf + got, (int)st.st_size - got)) > 0)
		got += n;
	if (n < 0 || got != (int)st.st_size) {
		perror("read");
		return 1;
	}
	buf[got] = 0;
	close(fd);

	ca.buf = buf;
	ca.len = (unsigned long)got + 1;	/* the NUL counts */

	sys = netfd();
	n = ioctl(sys, NETIOC_TLSCA, (char *)&ca);
	if (n < 0) {
		perror("NETIOC_TLSCA");
		fprintf(stderr, "tlsca: %s did not parse - is it PEM?\n",
			argv[1]);
		return 1;
	}
	printf("TLS: %d bytes loaded, certificates are %s\n", got,
	       n ? "CHECKED" : "NOT checked");
	return 0;
}

/*
  A Cheesey NTP client to set system time

  (C) 2017, Brett M. Gordon, GPL2 under Fuzix

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>
#include <getopt.h>
#include "netdb.h"

#define AF_INET     1
#define SOCK_DGRAM  2

typedef struct {
    uint32_t sec;
    uint32_t frac;
} ntime;

struct ntp_t {
    uint8_t lvm;
    uint8_t statum;
    uint8_t poll;
    uint8_t prec;
    uint32_t delay;
    uint32_t disp;
    uint32_t refid;
    ntime ref;
    ntime org;
    ntime rec;
    ntime xmit;
};


#define MAXBUF 256
int fd;
char buf[MAXBUF];
struct sockaddr_in addr;
int setflg = 0;
int disflg = 0;
int port = 123;  /* default port no */

void alarm_handler( int signum ){
    return;
}

void pusage( void ){
    fprintf(stderr, "ntpdate -sd [-o tz] [-O seconds] server\n");
    exit(1);
}

/* sends query to remote */
void sendq( void ){
    struct ntp_t *i = (struct ntp_t *)buf;
    memset( buf, 0, MAXBUF );
    i->lvm = 0xe3;
    write(fd, buf, 48);
}

void my_open( int argc, char *argv[]){
    struct hostent *h;

    h=gethostbyname( argv[optind] );
    if (!h){
	fprintf( stderr, "cannot resolve hostname\n" );
	exit(1);
    }
    memcpy( &addr.sin_addr.s_addr, h->h_addr_list[0], 4 );

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
	perror("af_inet sock_dgram 0");
	exit(1);
    }

    /* Network order.  Without this the query goes to port 0x7B00
       (31488) on a little-endian machine, which is why this has only
       ever worked where htons() is the identity. */
    addr.sin_port = htons(port);
    addr.sin_family = AF_INET;
    if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
	perror("connect");
	exit(1);
    }
}


int main( int argc, char *argv[] ){
    int retries;
    int rv;
    /* time_t, NOT uint32_t.  It is 64 bits on some Fuzix platforms
       and "ctime((time_t *)&uv)" then reads four bytes of stack past
       the end of it - which is what dated a good reply to 1890. */
    time_t uv = 0;
    int tz = 0;
    long tzsec = 0;
    struct ntp_t *ptr = (struct ntp_t *)buf;

    while ((rv = getopt( argc, argv, "p:o:O:ds" )) > 0 ){
	switch (rv){
	case 'p':
	    port = atoi( optarg );
	    break;
	case 'o':
	    tz = atoi( optarg );
	    if (tz < -12 || tz > 12){
		fprintf(stderr, "bad timezone\n");
		exit(1);
	    }
	    break;
	case 'O':
	    /* an offset in whole SECONDS, on top of -o: half-hour and
	       quarter-hour zones exist and -o's integer hours cannot
	       say them.  No range gate: the caller (BASIC's WEB NTP)
	       has already applied MMBasic's own -12..14 hour check. */
	    tzsec = atol( optarg );
	    break;
	case 's':
	    setflg = 1;
	    break;
	case 'd':
	    disflg = 1;
	    break;
	case '?':
	    pusage();
	}
    }
    if( ! argv[optind] )
	pusage();

    my_open( argc, argv );

    retries = 3;
    while (retries--){
	sendq();
	signal( SIGALRM, alarm_handler );
	alarm(2);
	rv = read( fd, buf, MAXBUF);
	if (rv < 0 )
	    continue;
	if (rv >= sizeof( struct ntp_t ))
	    goto process;

    }
    fprintf(stderr, "timeout\n");
    exit(1);

 process:

    /* Same again: the timestamp arrives big-endian, and reading it raw
       on a little-endian machine dated this reply to 1869. */
    uv = (time_t)ntohl(ptr->xmit.sec);
    uv -= 2208988800LL;	/* 1900 -> 1970; too big for a signed long */
    uv += tz * 60 * 60;
    uv += tzsec;

    if (disflg || !setflg)
	printf(ctime(&uv));

    if (setflg){
	rv = stime(&uv);
	if (rv){
	    perror( "stime" );
	    exit(1);
	}
    }
    exit(0);
}

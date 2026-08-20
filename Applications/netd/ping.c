/*
  A Cheesey ipv4 ping client

  (C) 2017, Brett M. Gordon, GPL2 under Fuzix

  todo:
  * check for endian problems
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
#define _BSD_SOURCE		/* gettimeofday, for the round-trip time */
#include <sys/time.h>
#include "netdb.h"

/*
  The clock for the round-trip time.

  gettimeofday() is the portable answer and it is not good enough here:
  Fuzix's fills tv_usec with a hard zero, so every reply times as 0 ms.
  Nor is CLOCK_MONOTONIC - the tick is the finest thing the kernel
  offers a portable caller, and even at this port's 200Hz that is 5ms
  against LAN round trips of about the same.

  PC3_US_CLOCK (set by Makefile.armm0) uses the Pico Computer 3's
  microsecond timer instead.  It is three register loads and no syscall
  - there is no MMU on that board - so the measurement costs nothing
  against the thing being measured.  Everywhere else this falls back to
  gettimeofday and prints whole seconds, as before.
*/
#ifdef PC3_US_CLOCK
#include <sys/pc3io.h>
#endif

#define AF_INET     1
#define SOCK_RAW    1

struct ip {
    uint8_t ver;
    uint8_t tos;
    uint16_t len;
    uint16_t id;
    uint16_t off;
    uint8_t ttl;
    uint8_t proto;
    uint16_t cksum;
    uint32_t src;
    uint32_t dest;
};

struct icmp {
    uint8_t type;
    uint8_t code;
    uint16_t cksum;
    uint16_t id;
    uint16_t seq;
};

char *data = "FUZIX ping client";

#define MAXBUF 256
int fd;
char buf[MAXBUF];
struct sockaddr_in addr;
int id;
int seq=0;
int sent=0;
int nrecv=0;
int count=0;			/* -c: 0 means until interrupted */
/* all in microseconds, whatever the clock underneath */
long rtt_min=-1;
long rtt_max=0;
long rtt_sum=0;

void alarm_handler( int signum ){
    return;
}

#ifdef PC3_US_CLOCK

typedef long long pingtime_t;

pingtime_t time_now( void ){
    return pc3_us64();
}

long us_since( pingtime_t t0 ){
    return (long)(pc3_us64() - t0);
}

#else

typedef struct timeval pingtime_t;

pingtime_t time_now( void ){
    struct timeval t;

    gettimeofday( &t, NULL );
    return t;
}

long us_since( pingtime_t t0 ){
    struct timeval now;

    gettimeofday( &now, NULL );
    return (now.tv_sec - t0.tv_sec) * 1000000L
	 + (long)now.tv_usec - (long)t0.tv_usec;
}

#endif

/* microseconds as milliseconds to one decimal place, without floats */
void msprint( long us ){
    printf("%ld.%ld", us/1000L, (us%1000L)/100L );
}

void stats( void ){
    printf("sent %d, recv %d, %d%%\n",
	   sent, nrecv, sent ? nrecv*100/sent : 0 );
    if( nrecv ){
	printf("rtt min/avg/max = ");
	msprint( rtt_min );
	printf("/");
	msprint( rtt_sum/nrecv );
	printf("/");
	msprint( rtt_max );
	printf(" ms\n");
    }
}

void int_handler( int signum ){
    stats();
    exit(0);
}

/* print a IP address.  Unsigned: as char these came out as
   "-64.-88.1.-2" on a machine with a signed plain char. */
void ipprint( uint32_t *a ){
    unsigned char *b = (unsigned char *)a;
    printf("%d.%d.%d.%d", b[0], b[1], b[2], b[3] );
}


/* returns inet chksum */
uint16_t cksum( char *b, int len ){
    uint16_t sum = 0;
    uint16_t t;
    char *e = b + len;
    b[len] = 0;
    while(b < e){
	t = (b[0] << 8) + b[1];
	sum += t;
	if(sum < t) sum++;
	b += 2;
    }
    return ~sum;
}


/* sends ping to remote */
void sendping( void ){
    struct icmp *i = (struct icmp *)buf;
    int l = strlen(data) + 8;
    memset( buf, 0, MAXBUF);
    i->type = 8;  // echo request
    i->id = htons(id);
    i->seq = htons(seq);
    strcpy( &buf[8], data );
    i->cksum = htons(cksum(buf, l));
    write(fd, buf, l);
    sent++;
    seq++;
}

void my_open( int argc, char *argv[]){
    struct hostent *h;

    h=gethostbyname( argv[1] );
    if( ! h ){
	fprintf( stderr, "cannot resolve hostname\n" );
	exit(1);
    }
    memcpy( &addr.sin_addr.s_addr, h->h_addr_list[0], 4 );

    fd = socket(AF_INET, SOCK_RAW, 1);
    if (fd < 0) {
	perror("socket");
	exit(1);
    }

    addr.sin_port = 0;
    addr.sin_family = AF_INET;
    if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
	perror("connect");
	exit(1);
    }
}


int main( int argc, char *argv[] ){
    int x;
    time_t t;
    long rtt;
    pingtime_t t_send;
    struct icmp *icmpbuf;
    struct ip *ipbuf;
    struct ip *ipbuf2;
    char *host;

    srand(time(&t));
    id = rand();

    signal(SIGINT, int_handler);

    if( argc > 2 && !strcmp( argv[1], "-c" ) ){
	count = atoi( argv[2] );
	argv += 2;
	argc -= 2;
    }
    if( argc < 2 ){
	fprintf( stderr,"usage: ping [-c count] hostname\n");
	exit(1);
    }
    host = argv[1];

    my_open( argc, argv );


    while( !count || sent < count ){
	t_send = time_now();
	sendping();
	/* FIXME: this breaks if the alarm occurs before the read under
	   load - sigsetjmp/siglongjmp needed I think */
	signal( SIGALRM, alarm_handler );
	alarm(2);
    ragain:
	x=read( fd, buf, MAXBUF);
	if (x > 0){
	    ipbuf = (struct ip *)buf;
	    if (ipbuf->ver >> 4 != 4) {
	        printf("Not v4 ?\n");
		goto ragain;
            }
	    icmpbuf = (struct icmp *)(buf + (ipbuf->ver & 15) * 4);
	    /* check for dest unreachable icmp messages */
	    if ( icmpbuf->type == 3 ){
		/* point to original ip packet in icmp data field */
		ipbuf2 = (struct ip *)(icmpbuf + 1);
		icmpbuf = (struct icmp *)((char *)ipbuf2 + (ipbuf2->ver & 15) * 4);
		/* check the bombed-out ip packet to see if it's ours */
		if( icmpbuf->id == htons(id) ){
		    printf("ICMP: from ");
		    ipprint( &ipbuf->src );
		    printf(" dest unreachable\n");
		}
		goto ragain;
	    }	
	    /* filter for our id */
	    if( icmpbuf->id != htons(id) ) {
	        printf("Bad id %d\n", ntohs(icmpbuf->id));
		goto ragain;
            }
	    /* passed filters, so this must be one of our pings */
	    nrecv++;
	    rtt = us_since( t_send );
	    if( rtt_min < 0 || rtt < rtt_min ) rtt_min = rtt;
	    if( rtt > rtt_max ) rtt_max = rtt;
	    rtt_sum += rtt;
	    printf("%d bytes from %s (", x, host );
	    ipprint( &ipbuf->src );
	    printf(") req=%d time=", ntohs(icmpbuf->seq));
	    msprint( rtt );
	    printf(" ms\n");
	}
	if( count && sent >= count )
	    break;
	sleep(1);
    }
    stats();
    return 0;
}

/*

   A simple http server

   It used to bind, listen, accept once, print "httpd server" and exit
   without answering - which meant the only thing it proved was that
   accept() returns.  A client saw a connection open and close with no
   bytes in it, which reads exactly like a server that is not there.

   Now it serves, in a loop, one connection at a time:

	httpd [port]		default 8080

   Files come from the directory it was started in.  "/" is index.html,
   and a path containing ".." or a leading "/" after the first is
   refused - there is no chroot here and the whole disc is one namespace.

 */
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


struct sockaddr_in addr;
struct sockaddr_in laddr;
int lfd;

#define BUFSIZE 512
static char buf[BUFSIZE];

static const char notfound[] =
    "HTTP/1.0 404 Not Found\r\n"
    "Content-Type: text/plain\r\n"
    "Connection: close\r\n"
    "\r\n"
    "not found\r\n";

static const char badreq[] =
    "HTTP/1.0 400 Bad Request\r\n"
    "Content-Type: text/plain\r\n"
    "Connection: close\r\n"
    "\r\n"
    "bad request\r\n";

static void saydone( int fd, const char *s ){
    write( fd, s, strlen(s) );
}

/*  Pull the path out of "GET /what/ever HTTP/1.0".  Returns NULL if
    this is not something we are willing to open. */
static char *getpath( char *req ){
    char *p, *e;

    if( strncmp( req, "GET ", 4 ) )
	return NULL;
    p = req + 4;
    for( e = p; *e && *e != ' ' && *e != '\r' && *e != '\n'; e++ )
	;
    *e = 0;
    if( *p != '/' )
	return NULL;
    p++;				/* relative to the cwd */
    if( strstr( p, ".." ) )
	return NULL;
    if( !*p )
	return "index.html";
    return p;
}

static void serve( int fd ){
    char *path;
    int n, in;

    n = read( fd, buf, BUFSIZE - 1 );
    if( n <= 0 )
	return;
    buf[n] = 0;

    path = getpath( buf );
    if( !path ){
	saydone( fd, badreq );
	return;
    }
    in = open( path, O_RDONLY );
    if( in < 0 ){
	saydone( fd, notfound );
	return;
    }
    /*  No Content-Length: the close is the end of the body, which is
	what HTTP/1.0 with "Connection: close" means and what saves us
	stat()ing the file first. */
    saydone( fd,
	     "HTTP/1.0 200 OK\r\n"
	     "Connection: close\r\n"
	     "\r\n" );
    while( (n = read( in, buf, BUFSIZE )) > 0 )
	if( write( fd, buf, n ) != n )
	    break;
    close( in );
}

void my_open( int argc, char *argv[]){
    int port = 8080;    /* default port */

    if( argc > 1 )
	port = atoi( argv[1] );

    addr.sin_port = htons(port);
    addr.sin_family = AF_INET;

    lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
	perror("af_inet sock_stream 0");
	exit(1);
    }
    if( bind( lfd, (struct sockaddr *)&addr, sizeof(addr) ) ){
	perror("bind");
	exit(1);
    }

    if( listen( lfd, 1 ) ){
	perror("listen");
	exit(1);
    }
    printf("httpd: serving this directory on port %d\n", port);
    fflush(stdout);
}

int main( int argc, char *argv[]){
	int fd;

	/*  A client that goes away mid-body must not take the server
	    with it. */
	signal( SIGPIPE, SIG_IGN );

	my_open( argc, argv );

	while(1){
	    fd = accept(lfd, NULL, NULL);
	    if( fd < 0 ){
		if( errno == EINTR )
		    continue;
		perror("accept");
		return 1;
	    }
	    serve( fd );
	    close( fd );
	}
}

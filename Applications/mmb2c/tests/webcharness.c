/* webcharness - the WEB TCP client against a real server, on the host.
 *
 * The .bas gates cannot carry a TCP peer, so the client family's
 * observable semantics are proven here instead: a forked server on
 * 127.0.0.1 speaks the shapes MMTCPclient.c defines, and every rule
 * PLAN-web.md §3.1 promises is asserted -
 *
 *   - READ collects an unsolicited greeting;
 *   - REQUEST DISCARDS unread input before writing (the SMTP rule:
 *     junk sent between exchanges must not come back as the next
 *     command's answer);
 *   - a reply arriving in two segments is joined by the 500 ms drain;
 *   - WRITE pushes a LONGSTRING payload intact (length checked at the
 *     far end);
 *   - a second OPEN replaces the first connection.
 *
 * Build and run: part of make check's harness pass; exit status is the
 * verdict.
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>
#include "mmb_runtime.h"

/* ---- the little the headers need from the runtime ------------------- */

void mm_error(const char *msg)
{
    fprintf(stderr, "webcharness: mm_error: %s\n", msg);
    exit(1);
}

MMINTEGER mm_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (MMINTEGER)tv.tv_sec * 1000000 + tv.tv_usec;
}

void mm_pause(MMFLOAT ms)
{
    usleep((useconds_t)(ms * 1000.0));
}

#include "mmb_net.h"
#include "mmb_webc.h"

/* ---- helpers -------------------------------------------------------- */

static void mstr(char *dst, const char *s)
{
    size_t n = strlen(s);
    dst[0] = (char)n;
    memcpy(dst + 1, s, n);
}

static int fails;

static void expect_buf(const char *what, MMINTEGER *a, const char *want)
{
    long n = (long)a[0];
    if (n != (long)strlen(want) || memcmp(&a[1], want, n) != 0) {
        fprintf(stderr, "FAIL %s: got %ld bytes \"%.*s\", want \"%s\"\n",
                what, n, (int)n, (char *)&a[1], want);
        fails++;
    } else
        printf("ok %s\n", what);
}

/* read one LF-terminated line, blocking, at the server end */
static int sline(int fd, char *buf, int max)
{
    int n = 0;
    while (n < max - 1) {
        if (read(fd, buf + n, 1) != 1)
            return -1;
        if (buf[n++] == '\n')
            break;
    }
    buf[n] = 0;
    return n;
}

static void ssend(int fd, const char *s)
{
    if (write(fd, s, strlen(s)) < 0)
        _exit(3);
}

#define PAYLOAD 3000

/* ---- the server ----------------------------------------------------- */

static void serve(int lfd)
{
    char line[256];
    long got;
    int fd, n;
    static char pay[PAYLOAD];

    /* connection 1: greeting, EHLO -> a two-segment reply, DATA, a
       PAYLOAD-byte body, a final status */
    fd = accept(lfd, NULL, NULL);
    if (fd < 0)
        _exit(3);
    ssend(fd, "220 hi\r\n");
    if (sline(fd, line, sizeof(line)) < 0 || strcmp(line, "EHLO\r\n"))
        _exit(4);
    ssend(fd, "250-A\r\n");
    usleep(60 * 1000);          /* a second segment, late */
    ssend(fd, "250 B\r\n");
    if (sline(fd, line, sizeof(line)) < 0 || strcmp(line, "DATA\r\n"))
        _exit(5);
    ssend(fd, "354 go\r\n");
    got = 0;
    while (got < PAYLOAD) {
        n = read(fd, pay + got, PAYLOAD - got);
        if (n <= 0)
            _exit(6);
        got += n;
    }
    for (n = 0; n < PAYLOAD; n++)
        if ((unsigned char)pay[n] != (unsigned char)(n & 0xFF))
            _exit(7);
    ssend(fd, "250 sent\r\n");
    close(fd);

    /* connection 2: junk the client never read, then a real exchange -
       the junk must be discarded by the next REQUEST */
    fd = accept(lfd, NULL, NULL);
    if (fd < 0)
        _exit(3);
    ssend(fd, "JUNK JUNK\r\n");
    if (sline(fd, line, sizeof(line)) < 0 || strcmp(line, "PING\r\n"))
        _exit(8);
    ssend(fd, "PONG\r\n");
    close(fd);
    _exit(0);
}

/* ---- the driver ----------------------------------------------------- */

int main(void)
{
    static MMINTEGER a[520];
    static MMINTEGER ls[(PAYLOAD / 8) + 2];
    char host[20], req[80];
    unsigned char sa[16], loop[4] = { 127, 0, 0, 1 };
    struct sockaddr_in bound;
    socklen_t bl = sizeof(bound);
    int lfd, port, st;
    long i;
    pid_t pid;

    signal(SIGPIPE, SIG_IGN);   /* as bcrun does for real programs */

    lfd = socket(AF_INET, SOCK_STREAM, 0);
    mmn_sin(sa, loop, 0);
    if (lfd < 0 || bind(lfd, (struct sockaddr *)sa, 16) < 0 ||
        listen(lfd, 2) < 0 ||
        getsockname(lfd, (struct sockaddr *)&bound, &bl) < 0) {
        perror("listener");
        return 2;
    }
    port = ntohs(bound.sin_port);

    pid = fork();
    if (pid == 0)
        serve(lfd);
    close(lfd);

    mstr(host, "127.0.0.1");
    mmg_webc_open(host, port, 3000, 0);

    /* the greeting: READ's whole purpose */
    mmg_webc_read(a, 520, 2000);
    expect_buf("read-greeting", a, "220 hi\r\n");

    /* a two-segment reply joined by the 500 ms drain */
    mstr(req, "EHLO\r\n");
    mmg_webc_request(req, a, 520, 2000);
    expect_buf("request-two-segments", a, "250-A\r\n250 B\r\n");

    mstr(req, "DATA\r\n");
    mmg_webc_request(req, a, 520, 2000);
    expect_buf("request-simple", a, "354 go\r\n");

    /* a LONGSTRING body, verified byte for byte at the far end */
    ls[0] = PAYLOAD;
    for (i = 0; i < PAYLOAD; i++)
        ((char *)&ls[1])[i] = (char)(i & 0xFF);
    mmg_webc_write(ls, 5000);
    mmg_webc_read(a, 520, 2000);
    expect_buf("write-payload-acked", a, "250 sent\r\n");

    /* reopen replaces the connection; the junk the server sent first
       sits unread until REQUEST discards it */
    mmg_webc_open(host, port, 3000, 0);
    mm_pause(200);              /* let the junk arrive and queue */
    mstr(req, "PING\r\n");
    mmg_webc_request(req, a, 520, 2000);
    expect_buf("request-discards-unread", a, "PONG\r\n");

    mmg_webc_close();

    if (waitpid(pid, &st, 0) < 0 || !WIFEXITED(st) ||
        WEXITSTATUS(st) != 0) {
        fprintf(stderr, "FAIL server exited %d\n",
                WIFEXITED(st) ? WEXITSTATUS(st) : -1);
        fails++;
    } else
        printf("ok server-side-checks\n");

    if (fails) {
        fprintf(stderr, "webcharness: %d failure(s)\n", fails);
        return 1;
    }
    printf("webcharness: all passed\n");
    return 0;
}

/* websharness - the WEB TCP server against a real client, on the host.
 *
 * The .bas gate (webservz) covers the peer-less semantics; a
 * one-process client-and-server round trip would deadlock by design
 * (interrupts fire at statement boundaries, on both firmwares), so
 * the live paths are proven here with a forked plain-socket client:
 *
 *   - accept into a slot, READ hands over the request ONCE
 *     (inttrig consumed), nulls arrive as spaces;
 *   - TRANSMIT CODE is the three-digits-over-404 status line;
 *   - TRANSMIT FILE sends the reference header and the exact bytes;
 *   - SEND pushes raw payload;
 *   - a second connection lands in the next slot.
 *
 * Exit status is the verdict, as make check's harness pass expects.
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
    fprintf(stderr, "websharness: mm_error: %s\n", msg);
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

void *mm_lheap(unsigned long n)
{
    return calloc(1, n);
}

void mm_lfree(void *p)
{
    free(p);
}

void mm_int_to_str(char *p, long long v, int base)
{
    (void)base;
    sprintf(p, "%lld", v);
}

void mm_float_to_str(char *p, MMFLOAT v, int m, int n, unsigned char c)
{
    (void)m; (void)n; (void)c;
    sprintf(p, "%g", (double)v);
}

MMINTEGER mm_filesize(const char *m)
{
    char path[256];
    FILE *f;
    long sz;
    int n = (unsigned char)m[0];

    memcpy(path, m + 1, n);
    path[n] = 0;
    f = fopen(path, "rb");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fclose(f);
    return sz;
}

#include "mmb_net.h"
#include "mmb_webs.h"

static void mstr(char *dst, const char *s)
{
    size_t n = strlen(s);
    dst[0] = (char)n;
    memcpy(dst + 1, s, n);
}

static int fails;

static void check(const char *what, int ok)
{
    if (ok)
        printf("ok %s\n", what);
    else {
        fprintf(stderr, "FAIL %s\n", what);
        fails++;
    }
}

/* wait for a slot to hold a request, pumping the poll */
static int pump_until_trig(int deadline_ms)
{
    int i, t;

    for (t = 0; t < deadline_ms; t += 5) {
        mmg_webs_poll();
        for (i = 0; i < MMW_NSLOT; i++)
            if (mm_webs_trig[i])
                return i;
        usleep(5000);
    }
    return -1;
}

/* ---- the client (forked) -------------------------------------------- */

static int cconnect(int port)
{
    unsigned char sa[16], loop[4] = { 127, 0, 0, 1 };
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    mmn_sin(sa, loop, port);
    if (fd < 0 || connect(fd, (struct sockaddr *)sa, 16) < 0)
        _exit(3);
    return fd;
}

static long creadall(int fd, char *buf, long max)
{
    long got = 0;
    int n;

    for (;;) {
        n = read(fd, buf + got, max - got);
        if (n <= 0)
            break;
        got += n;
    }
    return got;
}

static void client(int port)
{
    char buf[4096];
    long n;
    int fd;

    /* exchange 1: a request with an embedded NUL; expect 204 */
    fd = cconnect(port);
    if (write(fd, "AB\0CD", 5) != 5)
        _exit(4);
    n = creadall(fd, buf, sizeof(buf));
    if (n != 16 || memcmp(buf, "HTTP/1.0 204\r\n\r\n", 16))
        _exit(5);
    close(fd);

    /* exchange 2: expect the served file, header and body */
    fd = cconnect(port);
    if (write(fd, "GET", 3) != 3)
        _exit(6);
    n = creadall(fd, buf, sizeof(buf));
    buf[n > 0 ? n : 0] = 0;
    if (!strstr(buf, "HTTP/1.1 200 OK\r\nServer:CPi\r\n"))
        _exit(7);
    if (!strstr(buf, "Content-type:text/plain\r\nContent-Length:26\r\n\r\n"))
        _exit(8);
    if (!strstr(buf, "abcdefghijklmnopqrstuvwxyz"))
        _exit(9);
    close(fd);

    /* exchange 3: raw SEND then server close */
    fd = cconnect(port);
    if (write(fd, "PING", 4) != 4)
        _exit(10);
    n = creadall(fd, buf, sizeof(buf));
    if (n != 4 || memcmp(buf, "PONG", 4))
        _exit(11);
    close(fd);

    /* exchange 4: TRANSMIT PAGE - substitution, the '{{' escape, and
       the CRLF CRLF tail inside the counted body */
    fd = cconnect(port);
    if (write(fd, "PAGE", 4) != 4)
        _exit(12);
    n = creadall(fd, buf, sizeof(buf));
    buf[n > 0 ? n : 0] = 0;
    if (!strstr(buf, "HTTP/1.1 200 OK\r\nServer:CPi\r\n"))
        _exit(13);
    if (!strstr(buf, "Content-type:text/html\r\nContent-Length:15\r\n"))
        _exit(14);
    if (!strstr(buf, "\r\n\r\nT=42 N={X}}\r\n\r\n"))
        _exit(15);
    close(fd);
    _exit(0);
}

/* ---- the server side, driven exactly as generated code drives it ---- */

int main(void)
{
    static MMINTEGER req[257];
    static MMINTEGER pay[8];
    char fname[64], mime[32];
    struct sockaddr_in bound;
    socklen_t bl = sizeof(bound);
    FILE *f;
    int slot, port, st;
    pid_t pid;

    signal(SIGPIPE, SIG_IGN);

    /* a fixed high port: the BASIC-facing range check (1-65535) is
       right to refuse the ephemeral 0 */
    port = 48200;
    mmg_webs_port(port);
    (void)bound; (void)bl;

    f = fopen("websharness.dat", "w");
    fputs("abcdefghijklmnopqrstuvwxyz", f);
    fclose(f);

    pid = fork();
    if (pid == 0)
        client(port);

    /* exchange 1: request in, nulls to spaces, 204 out */
    slot = pump_until_trig(3000);
    check("request-arrives", slot >= 0);
    if (slot >= 0) {
        mmg_webs_read(slot + 1, req, 257);
        check("read-nulls-spaced", req[0] == 5 &&
              memcmp(&req[1], "AB CD", 5) == 0);
        mmg_webs_read(slot + 1, req, 257);
        check("read-consumed", req[0] == 0);
        mmg_webs_code(slot + 1, 204);
    }

    /* exchange 2: serve the file */
    slot = pump_until_trig(3000);
    check("second-connection", slot >= 0);
    if (slot >= 0) {
        mstr(fname, "websharness.dat");
        mstr(mime, "text/plain");
        mmg_webs_file(slot + 1, fname, mime);
    }

    /* exchange 3: raw SEND, then close */
    slot = pump_until_trig(3000);
    check("third-connection", slot >= 0);
    if (slot >= 0) {
        pay[0] = 4;
        memcpy(&pay[1], "PONG", 4);
        mmg_webs_send(slot + 1, pay);
        mmg_webs_close(slot + 1);
    }

    /* exchange 4: the page engine, driven exactly as the emitted
       switch drives it */
    slot = pump_until_trig(3000);
    check("page-connection", slot >= 0);
    if (slot >= 0) {
        static const char *const ptab[] = { "T" };
        struct mm_webpg pg;
        int pi, hits = 0;

        f = fopen("websharness.htm", "w");
        fputs("T={ t } N={{X}}", f);
        fclose(f);
        mstr(fname, "websharness.htm");
        mm_webpg_start(&pg, slot + 1, fname, 64);
        while ((pi = mm_webpg_next(&pg, ptab, 1)) >= 0) {
            if (pi == 0) {
                mm_webpg_put_i(&pg, 42);
                hits++;
            }
        }
        check("page-substituted-once", hits == 1);
        mm_webpg_send(&pg);
        unlink("websharness.htm");
    }

    if (waitpid(pid, &st, 0) < 0 || !WIFEXITED(st) ||
        WEXITSTATUS(st) != 0) {
        fprintf(stderr, "FAIL client exited %d\n",
                WIFEXITED(st) ? WEXITSTATUS(st) : -1);
        fails++;
    } else
        printf("ok client-side-checks\n");

    unlink("websharness.dat");
    if (fails) {
        fprintf(stderr, "websharness: %d failure(s)\n", fails);
        return 1;
    }
    printf("websharness: all passed\n");
    return 0;
}

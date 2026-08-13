/* fifotest - Phase 0 spike for PLAN-games.md (player control FIFO).
 *
 * This kernel's FIFO semantics differ from POSIX in ways that shape
 * the playsnd/playmod control protocol (syscall_fs3.c:158-175):
 *
 *   - the opener bumps c_writers/c_readers BEFORE the pipe checks, so
 *     an O_NDELAY open for write (or rdwr) ALWAYS succeeds - a failed
 *     open cannot signal "daemon not running".  Discovery must use
 *     SNDIOC_PCMOWNER instead, which the plan already does for STOP;
 *   - any FIFO open without O_NDELAY psleeps once (o_refs==1),
 *     whoever is already there - so every open in the protocol
 *     carries O_NDELAY;
 *   - a write when no reader holds the far end raises EPIPE+SIGPIPE
 *     (inode.c wait_pipe_write) - the client's dead-daemon signal.
 *
 * Prove here:
 *   1. mkfifo works without superuser;
 *   2. daemon end: open(O_RDWR|O_NDELAY) succeeds on an idle FIFO,
 *      and read() of the empty FIFO gives -1/EAGAIN (self counts as
 *      writer, so never a false EOF);
 *   3. client end: open(O_WRONLY|O_NDELAY) succeeds, 16-byte records
 *      arrive whole across a fork boundary;
 *   4. with the daemon end closed, a client write gives EPIPE (with
 *      SIGPIPE ignored), not a wedge.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define PATH "/tmp/fifotest"
#define NREC 5

struct rec {
    unsigned char ver, op, a, b;
    long p1, p2, p3;
};

int main(void)
{
    struct rec r;
    int fdr, fdw, pid, status, n, i, got = 0, tries = 0, ok = 1;

    signal(SIGPIPE, SIG_IGN);
    unlink(PATH);
    if (mkfifo(PATH, 0666) < 0) {
        perror("mkfifo");
        return 1;
    }
    printf("mkfifo: ok\n");

    /* 2: the daemon end */
    fdr = open(PATH, O_RDWR | O_NDELAY);
    if (fdr < 0) {
        perror("daemon open rdwr");
        unlink(PATH);
        return 1;
    }
    printf("daemon open O_RDWR|O_NDELAY: ok\n");

    /* On this kernel an empty-pipe O_NDELAY read returns 0 with no
     * errno (wait_pipe_read zeroes u_count) - NOT -1/EAGAIN.  0 is
     * "quiet", never EOF, because we hold the write side ourselves. */
    n = read(fdr, &r, sizeof(r));
    printf("read empty: %d %s\n", n,
           n < 0 ? strerror(errno) : "(quiet; 0 expected here)");
    if (!(n == 0 || (n < 0 && errno == EAGAIN)))
        ok = 0;

    /* 3: a client across fork */
    pid = fork();
    if (pid == 0) {
        int i;
        fdw = open(PATH, O_WRONLY | O_NDELAY);
        if (fdw < 0) {
            perror("client open wr");
            _exit(1);
        }
        for (i = 0; i < NREC; i++) {
            memset(&r, 0, sizeof(r));
            r.ver = 1;
            r.op = (unsigned char)(i + 1);
            r.p1 = 1000 + i;
            if (write(fdw, &r, sizeof(r)) != sizeof(r)) {
                perror("client write");
                _exit(1);
            }
            if (i == 2)
                usleep(60000);  /* leave the parent a quiet poll */
        }
        close(fdw);
        _exit(0);
    }

    while (got < NREC && tries < 200) {
        n = read(fdr, &r, sizeof(r));
        if (n == (int)sizeof(r)) {
            if (r.ver != 1 || r.op != (unsigned char)(got + 1) ||
                r.p1 != 1000 + got) {
                printf("record %d CORRUPT (op %d p1 %ld)\n",
                       got, r.op, (long)r.p1);
                ok = 0;
                break;
            }
            got++;
        } else if (n > 0) {
            printf("SHORT read: %d bytes - records tear\n", n);
            ok = 0;
            break;
        } else if (n < 0 && errno != EAGAIN) {
            printf("read: %s\n", strerror(errno));
            ok = 0;
            break;
        } else {
            tries++;
            usleep(20000);
        }
    }
    printf("records: %d/%d intact, %d quiet polls\n", got, NREC, tries);
    if (got != NREC)
        ok = 0;
    wait(&status);
    close(fdr);

    /* 4: writing with the daemon gone.  On this kernel a small write
     * to a reader-less FIFO SUCCEEDS silently (EPIPE only past 4K of
     * backlog, inode.c wait_pipe_write) - so write failure can never
     * signal a dead daemon.  Discovery is SNDIOC_PCMOWNER, and the
     * daemon's startup unlink+mkfifo discards any stale records left
     * on the orphaned inode.  This leg just pins the behaviour. */
    pid = fork();
    if (pid == 0) {
        signal(SIGPIPE, SIG_IGN);
        fdw = open(PATH, O_WRONLY | O_NDELAY);
        if (fdw < 0) {
            perror("orphan open wr");
            _exit(2);
        }
        memset(&r, 0, sizeof(r));
        n = write(fdw, &r, sizeof(r));
        printf("orphan write: %d %s\n", n,
               n < 0 ? strerror(errno) : "(buffered; pinned behaviour)");
        _exit(n == (int)sizeof(r) || (n < 0 && errno == EPIPE) ? 0 : 3);
    }
    wait(&status);
    if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0))
        ok = 0;

    /* 5: a FRESH writer per record - the mm_play_send pattern.  Every
     * open() starts its fd offset at 0, so before the pipe carried its
     * own stream positions (cinode c_pipe_roff/woff) record 2 onward
     * overwrote record 1's bytes while the reader walked ahead into
     * never-written blocks and was handed zeros - playdemo's "morse
     * code on a single note".  Leg 3 never caught it because one fd
     * wrote all five records.  This is the regression test. */
    unlink(PATH);
    if (mkfifo(PATH, 0666) < 0) {
        perror("mkfifo 2");
        return 1;
    }
    fdr = open(PATH, O_RDWR | O_NDELAY);
    if (fdr < 0) {
        perror("daemon reopen rdwr");
        unlink(PATH);
        return 1;
    }
    for (i = 0; i < NREC; i++) {
        fdw = open(PATH, O_WRONLY | O_NDELAY);
        if (fdw < 0) {
            perror("reopen wr");
            ok = 0;
            break;
        }
        memset(&r, 0, sizeof(r));
        r.ver = 1;
        r.op = (unsigned char)(i + 1);
        r.p1 = 2000 + i;
        if (write(fdw, &r, sizeof(r)) != (int)sizeof(r)) {
            perror("reopen write");
            close(fdw);
            ok = 0;
            break;
        }
        close(fdw);
        n = read(fdr, &r, sizeof(r));
        if (n != (int)sizeof(r) || r.ver != 1 ||
            r.op != (unsigned char)(i + 1) || r.p1 != 2000 + i) {
            printf("reopen record %d %s (n=%d ver=%d op=%d p1=%ld)\n",
                   i, n == (int)sizeof(r) ? "CORRUPT" : "unreadable",
                   n, r.ver, r.op, (long)r.p1);
            ok = 0;
            break;
        }
    }
    if (i == NREC)
        printf("reopen-writer records: %d/%d intact\n", i, NREC);
    close(fdr);

    unlink(PATH);
    printf(ok ? "fifotest PASS\n" : "fifotest FAIL\n");
    return ok ? 0 : 1;
}

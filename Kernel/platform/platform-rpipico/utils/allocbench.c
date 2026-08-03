/* allocbench - what does one PSRAM arena alloc+free cost?
 *
 * The question it settles: can a translated BASIC routine take its
 * LOCAL arrays and strings from the kernel's PSRAM heap on every
 * invocation?  That is two ioctls per call, so the deciding number is
 * microseconds per alloc+free pair against the cost of the call itself.
 *
 * malloc/free is the reference - the same work out of the process's own
 * heap, which is the cheap alternative and needs no kernel at all.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "../pico_ioctl.h"

#define N 2000

static int fd;

static unsigned long usec(void)
{
    int sel = -9;
    return (unsigned long)ioctl(fd, PICOIOC_ADVAL, &sel);
}

static void report(const char *what, unsigned long us, unsigned long n)
{
    printf("  %-24s %7lu us / %lu = %6lu ns per pair\n",
           what, us, n, (us * 1000UL) / n);
}

int main(void)
{
    struct psram_req rq;
    unsigned long t0, i;
    uint32_t b;
    void *p;

    fd = open("/dev/sys", O_RDWR);
    if (fd < 0) {
        printf("allocbench: cannot open /dev/sys\n");
        return 1;
    }

    /* Warm-up: the first allocation may extend the break, which is not
       what a steady-state call pays. */
    rq.len = 256; rq.base = 0;
    ioctl(fd, PSRAMIOC_ALLOC, &rq);
    b = rq.base;
    ioctl(fd, PSRAMIOC_FREE, &b);

    printf("alloc+free, %d iterations of 256 bytes\n", N);

    t0 = usec();
    for (i = 0; i < N; i++) {
        rq.len = 256;
        rq.base = 0;
        if (ioctl(fd, PSRAMIOC_ALLOC, &rq) != 0 || !rq.base) {
            printf("  psram alloc failed at %lu\n", i);
            return 1;
        }
        b = rq.base;
        if (ioctl(fd, PSRAMIOC_FREE, &b) != 0) {
            printf("  psram free failed at %lu\n", i);
            return 1;
        }
    }
    report("psram arena (2 ioctls)", usec() - t0, N);

    t0 = usec();
    for (i = 0; i < N; i++) {
        p = malloc(256);
        if (p == NULL) {
            printf("  malloc failed at %lu\n", i);
            return 1;
        }
        free(p);
    }
    report("malloc/free (in process)", usec() - t0, N);

    /* Scale: an empty ioctl, so the pair above can be read as "two of
       these plus the allocator's own work". */
    t0 = usec();
    for (i = 0; i < N; i++)
        usec();
    report("one bare ioctl", usec() - t0, N);

    close(fd);
    return 0;
}

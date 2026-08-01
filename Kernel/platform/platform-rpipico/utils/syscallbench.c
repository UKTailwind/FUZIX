/* syscallbench - what does a Fuzix syscall cost on this machine?
 *
 * Decides whether the graphics primitives can live behind ioctls or
 * whether the per-pixel path needs the framebuffer address published.
 * MMBasic's PIXEL is a single memory store; if an ioctl costs enough,
 * a full-screen plot (76800 pixels) becomes visibly slower than
 * MMBasic, which would be the wrong trade on this project.
 *
 * Times a trivial syscall, a representative ioctl, and a plain memory
 * write for scale.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "../pico_ioctl.h"

#define N 5000

static int fd;
static volatile unsigned char scratch[256];

static unsigned long usec(void)
{
    int sel = -9;
    return (unsigned long)ioctl(fd, PICOIOC_ADVAL, &sel);
}

static void report(const char *what, unsigned long us, unsigned long n)
{
    /* ns per call, and what a 320x240 full-screen plot would cost */
    unsigned long ns = (us * 1000UL) / n;
    printf("  %-26s %7lu us / %lu = %5lu ns   full screen: %lu ms\n",
           what, us, n, ns, (ns * 76800UL) / 1000000UL);
}

int main(void)
{
    unsigned long t0, i;
    int sel = -9;

    fd = open("/dev/sys", O_RDWR);
    if (fd < 0) {
        perror("/dev/sys");
        return 1;
    }

    printf("syscall cost, %d iterations each\n", N);

    /* 1. the cheapest syscall there is */
    t0 = usec();
    for (i = 0; i < N; i++)
        getpid();
    report("getpid()", usec() - t0, N);

    /* 2. a representative ioctl - what a kernel primitive would use */
    t0 = usec();
    for (i = 0; i < N; i++)
        ioctl(fd, PICOIOC_ADVAL, &sel);
    report("ioctl(/dev/sys)", usec() - t0, N);

    /* 3. a plain memory write, for scale - this is what MMBasic's
     *    PIXEL does, and what a published framebuffer address gives */
    t0 = usec();
    for (i = 0; i < N; i++)
        scratch[i & 255] = (unsigned char)i;
    report("direct memory write", usec() - t0, N);

    close(fd);
    return 0;
}

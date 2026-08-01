/* memprobe - how much memory can one process actually have?
 *
 * Models the editor: a large static buffer (the flat edit buffer),
 * touched so it is really there, then sbrk() grown until it fails to
 * find the remaining headroom.  Answers whether the edit buffer can
 * live in the process's own SRAM instead of the PSRAM arena.
 *
 * Build with -DEDBUF=n to try a different buffer size in KB.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#ifndef EDBUF
#define EDBUF 120
#endif

static unsigned char edbuf[EDBUF * 1024];

int main(void)
{
    unsigned long i, got = 0;
    char *base, *p;

    /* Touch every page so the buffer is genuinely resident, not just
     * reserved - and check it reads back, which would catch the image
     * overlapping something it should not. */
    for (i = 0; i < sizeof(edbuf); i += 512)
        edbuf[i] = (unsigned char)(i >> 9);
    for (i = 0; i < sizeof(edbuf); i += 512)
        if (edbuf[i] != (unsigned char)(i >> 9)) {
            printf("static buffer CORRUPT at %lu\n", i);
            return 1;
        }
    printf("static buffer: %d KB, intact\n", EDBUF);

    base = sbrk(0);
    printf("break starts at %p\n", base);

    /* Grow in 4K steps until the kernel says no. */
    for (;;) {
        p = sbrk(4096);
        if (p == (char *)-1 || p == 0)
            break;
        got += 4096;
        if (got > 1024UL * 1024)        /* sanity stop */
            break;
    }
    printf("sbrk headroom above that: %lu KB\n", got / 1024);
    printf("so an editor could have %lu KB of buffer in its own space\n",
           (unsigned long)EDBUF + got / 1024);
    return 0;
}

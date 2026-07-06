#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

int main(void)
{
    struct flock fl;
    int fd;

    fd = open("testfile.dat", O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    fl.l_type = F_WRLCK;     /* try for write lock */
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;            /* whole file */

    printf("Trying to lock...\n");

    if (fcntl(fd, F_SETLKW, &fl) < 0) {
        perror("fcntl");

        if (errno == EACCES || errno == EAGAIN) {
            printf("Lock is already held by another process.\n");
        } else {
            printf("Unexpected error: %d\n", errno);
        }
    } else {
        printf("Lock acquired!\n");

        /* Hold it briefly so you can observe behaviour */
        sleep(5);

        printf("Releasing lock\n");

        fl.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &fl);
    }

    close(fd);
    return 0;
}

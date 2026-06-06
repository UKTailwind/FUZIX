#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main(void)
{
    struct flock fl;

    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    int fd = open("testfile.dat", O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (fcntl(fd, F_SETLK, &fl) < 0) {
        perror("fcntl");
        return 1;
    }

    printf("Lock acquired successfully\n");

    close(fd);
    return 0;
}

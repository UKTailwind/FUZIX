#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main(void)
{
    struct flock fl;

    int fd = open("testfile.dat", O_RDWR | O_CREAT, 0666);

    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    printf("Trying to lock...\n");

    if (fcntl(fd, F_SETLKW, &fl) < 0) {
        perror("fcntl");
        return 1;
    }

    printf("Lock acquired. Sleeping...\n");
    sleep(30);

    printf("Releasing lock\n");

    fl.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &fl);

    close(fd);
    return 0;
}

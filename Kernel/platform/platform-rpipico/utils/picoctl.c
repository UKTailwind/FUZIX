#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "../pico_ioctl.h"

int main(int argc, char **argv)
{
    if (argc == 1 || strcmp(argv[1], "--help") == 0)
    {
        puts("usage: picoctl [ --help ] <commmand>");
        puts("Command list:");
        puts("\tflash\tReset into flash mode.");
        puts("\tkeymap <cc>\tSet the USB keyboard layout (us uk de fr es be).");
        return 0;
    }
    int fd = open("/dev/sys", O_RDWR, 0);
    if (fd == -1)
    {
        perror("Failed to open /dev/sys");
        exit(1);
    }
    int r;
    if (strcmp(argv[1], "flash") == 0)
    {
        r = ioctl(fd, PICOIOC_FLASH);
    }
    else if (strcmp(argv[1], "keymap") == 0)
    {
        if (argc < 3 || strlen(argv[2]) != 2)
        {
            fputs("usage: picoctl keymap us|uk|de|fr|es|be\n", stderr);
            close(fd);
            exit(1);
        }
        r = ioctl(fd, PICOIOC_KBDMAP, argv[2]);
    }
    else
    {
        fputs("picoctl: unknown command\n", stderr);
        close(fd);
        exit(1);
    }
    if (r != 0)
    {
        perror("Failed to perform operation");
        close(fd);
        exit(1);
    }
    close(fd);
    return 0;
}

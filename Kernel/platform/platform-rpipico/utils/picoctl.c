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
        puts("\tmode [n]\tSet the display mode; no argument, or \"text\",");
        puts("\t\t\treturns to the 80x40 text console.");
        puts("\tusbreset\tRe-enumerate the USB bus (after the DPDT switch).");
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
    else if (strcmp(argv[1], "mode") == 0)
    {
        /* Nothing could set the display mode from the shell: gfxtest
         * cycles them as a test and puts the console back afterwards,
         * which left a BASIC program that ended in MODE 2 stranding the
         * screen there until a reboot.  "picoctl mode" with no argument
         * is the way back.
         *
         * These are the kernel's mode numbers, not MMBasic's: 0-5 are
         * the BBC modes, 7 is 320x240 in 16 colours (MMBasic's MODE 2),
         * and 0xFF is the text console (MMBasic's MODE 1 - the same
         * 640x480 1bpp framebuffer). */
        int mode = 0xFF;

        if (argc > 2 && strcmp(argv[2], "text") != 0)
            mode = (int)strtol(argv[2], NULL, 0);
        r = ioctl(fd, GFXIOC_MODE, &mode);
        if (r != 0 && mode != 0xFF)
            fputs("picoctl: no such display mode\n", stderr);
    }
    else if (strcmp(argv[1], "usbreset") == 0)
    {
        r = ioctl(fd, PICOIOC_USBRESET);
        if (r == 0)
            puts("USB bus reset; devices should re-enumerate.");
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

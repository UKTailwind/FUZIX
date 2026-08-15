#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "../pico_ioctl.h"

/*
 * Num lock is a property of the KEYBOARD (see usbkbd.c): a keyboard with
 * no numeric keypad overlays one onto 7890/uiop/jkl;/m while num lock is
 * on, and nothing in the USB descriptors identifies such a keyboard.  The
 * kernel remembers what it is told, by VID:PID - but only in RAM, because
 * a kernel does not write files.  This is the other half: a line per
 * keyboard here, written when a setting is changed and replayed by
 * /etc/rc, so a choice survives a reboot.
 *
 *	04d9:0006 off
 *	04b3:3025 on
 */
#define NUMLOCK_FILE "/etc/numlock"

static int nl_read(char *buf, int max)
{
    int fd = open(NUMLOCK_FILE, O_RDONLY, 0);
    int n;
    if (fd < 0)
        return 0;               /* nothing saved yet is not an error */
    n = read(fd, buf, max - 1);
    close(fd);
    if (n < 0)
        n = 0;
    buf[n] = 0;
    return n;
}

/* Rewrite the file with this keyboard's line replaced (or added), every
   other line kept as it was. */
static void nl_save(unsigned vid, unsigned pid, int on)
{
    char key[10];
    char buf[512];
    char out[600];
    int fd, n, i = 0, o = 0;

    sprintf(key, "%04x:%04x", vid, pid);
    n = nl_read(buf, sizeof(buf));

    while (i < n)
    {
        int start = i;
        while (i < n && buf[i] != '\n')
            i++;
        if (i < n)
            i++;                /* keep the newline with its line */
        if (strncmp(buf + start, key, 9) != 0)
        {
            memcpy(out + o, buf + start, i - start);
            o += i - start;
        }
    }
    /* A hand-edited file may have no newline on its last line; without
       this the appended entry would be jammed onto the end of it. */
    if (o && out[o - 1] != '\n')
        out[o++] = '\n';
    o += sprintf(out + o, "%s %s\n", key, on ? "on" : "off");

    fd = open(NUMLOCK_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        perror("picoctl: cannot write " NUMLOCK_FILE);
        return;
    }
    if (write(fd, out, o) != o)
        fputs("picoctl: short write to " NUMLOCK_FILE "\n", stderr);
    close(fd);
}

/* Push every saved setting into the kernel.  What /etc/rc runs. */
static void nl_load(int sysfd)
{
    char buf[512];
    int n = nl_read(buf, sizeof(buf));
    int i = 0;

    while (i < n)
    {
        char *line = buf + i;
        char *colon, *sp;
        struct kbd_numlock k;

        while (i < n && buf[i] != '\n')
            i++;
        if (i < n)
            buf[i++] = 0;
        colon = strchr(line, ':');
        sp = strchr(line, ' ');
        if (line[0] == '#' || colon == NULL || sp == NULL)
            continue;
        memset(&k, 0, sizeof(k));
        k.set = 1;
        k.vid = (unsigned)strtol(line, NULL, 16);
        k.pid = (unsigned)strtol(colon + 1, NULL, 16);
        k.on = (strncmp(sp + 1, "on", 2) == 0) ? 1 : 0;
        /* 0000:0000 would mean "the attached keyboard" to the kernel,
           which is not what a stored line can mean. */
        if (k.vid || k.pid)
            ioctl(sysfd, PICOIOC_NUMLOCK, &k);
    }
}

int main(int argc, char **argv)
{
    if (argc == 1 || strcmp(argv[1], "--help") == 0)
    {
        puts("usage: picoctl [ --help ] <commmand>");
        puts("Command list:");
        puts("\tflash\tReset into flash mode.");
        puts("\tkeymap <cc>\tSet the USB keyboard layout (us uk de fr es be).");
        puts("\tnumlock [on|off [vvvv:pppp] [--once]] | --load");
        puts("\t\t\tNum lock for this keyboard.  A keyboard with no");
        puts("\t\t\tkeypad types digits for 7890/uiop/jkl;/m with it");
        puts("\t\t\ton.  No argument reports.  Saved per keyboard in");
        puts("\t\t\t" NUMLOCK_FILE " and replayed by /etc/rc, so it");
        puts("\t\t\tsurvives a reboot; --once does not save.  A vid:pid");
        puts("\t\t\tsets a keyboard that is not plugged in.");
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
    else if (strcmp(argv[1], "numlock") == 0)
    {
        struct kbd_numlock k;
        int save = 1, i;
        memset(&k, 0, sizeof(k));

        if (argc > 2 && strcmp(argv[2], "--load") == 0)
        {
            nl_load(fd);        /* /etc/rc: replay every saved setting */
            close(fd);
            return 0;
        }
        for (i = 2; i < argc; i++)
        {
            if (strcmp(argv[i], "--once") == 0)
                save = 0;       /* this session only, do not record it */
            else if (strcmp(argv[i], "on") == 0 || strcmp(argv[i], "off") == 0)
            {
                k.on = (argv[i][1] == 'n');
                k.set = 1;
            }
            else
            {
                char *colon = strchr(argv[i], ':');
                if (colon == NULL)
                {
                    fputs("usage: picoctl numlock [on|off [vvvv:pppp] [--once]]\n",
                        stderr);
                    fputs("       picoctl numlock --load\n", stderr);
                    close(fd);
                    exit(1);
                }
                /* An explicit keyboard, so a setting can be made for one
                   that is not plugged in. */
                k.vid = (unsigned)strtol(argv[i], NULL, 16);
                k.pid = (unsigned)strtol(colon + 1, NULL, 16);
                if (k.vid == 0 && k.pid == 0)
                {
                    fputs("picoctl: 0000:0000 means the attached keyboard\n",
                        stderr);
                    close(fd);
                    exit(1);
                }
            }
        }
        if (argc > 2 && !k.set)
        {
            fputs("picoctl: numlock needs on or off\n", stderr);
            close(fd);
            exit(1);
        }
        /* Named or not, the ioctl reports the MOUNTED keyboard back, which
           is what a bare set is saved against. */
        {
            unsigned wvid = k.vid, wpid = k.pid;
            int won = k.on, wset = k.set;
            r = ioctl(fd, PICOIOC_NUMLOCK, &k);
            if (r == 0 && wset && save)
            {
                if (!wvid && !wpid)
                {
                    wvid = k.vid;
                    wpid = k.pid;
                }
                if (wvid || wpid)
                    nl_save(wvid, wpid, won);
                else
                    fputs("picoctl: no keyboard attached, nothing saved\n",
                        stderr);
            }
        }
        if (r == 0 && argc == 2)
        {
            /* Report, and say what the guess was based on: which keyboard
               this is, and whether it owns up to a num lock light. */
            printf("num lock %s\n", k.on ? "on" : "off");
            if (k.vid || k.pid)
                printf("keyboard %04x:%04x, num lock LED %s\n",
                    k.vid, k.pid, k.led ? "declared" : "NOT declared");
            else
                puts("no keyboard attached");
        }
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

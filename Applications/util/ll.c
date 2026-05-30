#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <time.h>

char *month[] = { "Jan", "Feb", "Mar", "Apr",
                  "May", "Jun", "Jul", "Aug",
                  "Sep", "Oct", "Nov", "Dec" };


void prmode(int mode)
{
    if (mode & 4)
        printf("r");
    else
        printf("-");

    if (mode & 2)
        printf("w");
    else
        printf("-");

    if (mode & 1)
        printf("x");
    else
        printf("-");
}

int ls(char *path)
{
    int st;
    struct dirent *d;
    static DIR dp;
    struct stat statbuf;
    static char dname[512];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    unsigned year = t->tm_year;

    if (stat(path, &statbuf) != 0 || !S_ISDIR(statbuf.st_mode)) {
        printf("ls: can't stat %s\n", path);
        return -1;
    }

    if (opendir_r(&dp, path) == NULL) {
        printf ("ls: can't open %s\n", path);
        return -1;
    }

    while ((d = readdir(&dp)) != NULL) {
        if (d->d_name[0] == '\0')
            continue;

        if (path[0] != '.' || path[1]) {
            strlcpy(dname, path, sizeof(dname));
            strlcat(dname, "/", sizeof(dname));
        } else {
            dname[0] = '\0';
        }

        strlcat(dname, d->d_name, sizeof(dname));

        if (stat(dname, &statbuf) != 0) {
            printf("ls: can't stat %s\n", dname);
            break;
        }

        st = statbuf.st_mode & S_IFMT;

        if (st == S_IFDIR)
            printf("d");
        else if (st == S_IFCHR)
            printf("c");
        else if (st == S_IFBLK)
            printf("b");
        else if (st == S_IFIFO)
            printf("p");
        else if ((st & S_IFREG) == 0)
            printf("l");
        else
            printf("-");

        prmode(statbuf.st_mode >> 6);
        prmode(statbuf.st_mode >> 3);
        prmode(statbuf.st_mode);

        printf("%4d %5d", statbuf.st_nlink, statbuf.st_ino);
        if (S_ISDIR(statbuf.st_mode))
            strlcat(dname, "/", sizeof(dname));
        else if (statbuf.st_mode & 0111)
            strlcat(dname, "*", sizeof(dname));

        printf("%12lu ",
                (S_ISBLK(statbuf.st_mode) || S_ISCHR(statbuf.st_mode)) ? 
                    statbuf.st_rdev : (unsigned
                    long)statbuf.st_size);

        if (statbuf.st_mtime == 0) {  /* st_mtime? */
            printf("                   ");
        } else {
            t = localtime(&statbuf.st_mtime);
            printf("%s %02d ", month[t->tm_mon], t->tm_mday);
            if (t->tm_year != year) {
                printf( " %4d",
                       t->tm_year + 1900);
            } else 
                printf("%2d:%02d",
                       t->tm_hour,
                       t->tm_min);
        }

        printf("  %s\n", dname);
    }
    closedir_r(&dp);
    return 0;
}

int main(int argc, char *argval[])
{
    if (argc < 2)
        return ls(".");
    else
        return ls(argval[1]);
}

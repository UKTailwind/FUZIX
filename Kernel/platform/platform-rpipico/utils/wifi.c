/*
 * wifi - join a network, or say what the radio is doing.
 *
 *	wifi				status
 *	wifi <ssid> <key> [auth]	join, and wait for an address
 *	wifi -f [file]			join using saved credentials
 *	wifi down			leave
 *
 * auth is 0 open, 1 WPA-TKIP, 2 WPA2-AES (the default), 3 mixed.
 *
 * The credentials file is /etc/wifi.conf, one line:
 *
 *	ssid key [auth]
 *
 * It holds a password, so it is created 0600 and this program says so
 * if it finds otherwise.  Nothing writes it automatically: a password
 * belongs in a file the owner made, not in one a program guessed at.
 *
 * Joining is asynchronous in the kernel (NETIOC_UP returns as soon as
 * the association is started), so the waiting happens HERE, in a
 * process that can be interrupted, rather than inside a syscall that
 * would stop the machine.  See PC3-NET-PLAN.md.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "../pico_ioctl.h"

#define CONF "/etc/wifi.conf"

static int sysfd;

static void putip(const char *label, unsigned long a)
{
    printf("%s %lu.%lu.%lu.%lu\n", label,
           (a >> 24) & 0xff, (a >> 16) & 0xff, (a >> 8) & 0xff, a & 0xff);
}

/* cyw43_tcpip_link_status: 0 down, 1 joining, 2 no IP yet, 3 up.
   Negative values are the driver's own failures. */
static const char *linkname(int link, int wifi)
{
    if (wifi < 0)
        return wifi == -2 ? "no such network" :
               wifi == -3 ? "authentication failed" : "failed";
    switch (link) {
    case 0: return "down";
    case 1: return "joining";
    case 2: return "joined, no address";
    case 3: return "up";
    }
    return "?";
}

static int status(int verbose)
{
    struct net_status st;

    if (ioctl(sysfd, NETIOC_STATUS, &st) < 0) {
        perror("NETIOC_STATUS");
        return 1;
    }
    if (!st.present) {
        printf("no radio (Pico Computer 2)\n");
        return 1;
    }
    if (!st.ready) {
        printf("radio off\n");
        return 0;
    }
    printf("%s", linkname(st.link, st.wifi));
    if (st.rssi)
        printf(", %ld dBm", (long)st.rssi);
    printf("\n");
    if (verbose || st.link == 3) {
        printf("mac %02x:%02x:%02x:%02x:%02x:%02x\n",
               st.mac[0], st.mac[1], st.mac[2],
               st.mac[3], st.mac[4], st.mac[5]);
        putip("ip     ", (unsigned long)st.ip);
        putip("netmask", (unsigned long)st.mask);
        putip("gateway", (unsigned long)st.gw);
    }
    return st.link == 3 ? 0 : 1;
}

/* Read "ssid key [auth]" out of the credentials file.
 *
 * A line at a time, because the file is mostly comment: the shipped
 * pro-forma is 629 bytes of explanation with the credentials at the
 * bottom, and a read() into a fixed buffer saw only the comments and
 * announced there were no credentials.  That is what the board said
 * the first time this ran. */
static int readconf(const char *path, struct net_join *j)
{
    struct stat s;
    char buf[160];
    char *p, *q;
    FILE *f;

    f = fopen(path, "r");
    if (f == NULL) {
        perror(path);
        return -1;
    }
    if (stat(path, &s) == 0 && (s.st_mode & 077))
        fprintf(stderr, "%s: readable by others - chmod 600 it\n", path);

    /* First line that is not blank and not a comment.  The file is
       meant to be edited by hand, so it has to be allowed to say what
       its own format is. */
    for (;;) {
        if (fgets(buf, sizeof(buf), f) == NULL) {
            fclose(f);
            fprintf(stderr, "%s: no credentials, only comments\n", path);
            return -1;
        }
        p = strchr(buf, '\n');
        if (p)
            *p = 0;
        p = buf;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p && *p != '#')
            break;
    }
    fclose(f);

    q = strchr(p, ' ');
    if (!q) {
        fprintf(stderr, "%s: want \"ssid key [auth]\"\n", path);
        return -1;
    }
    *q++ = 0;
    strncpy(j->ssid, p, sizeof(j->ssid) - 1);

    while (*q == ' ')
        q++;
    p = strchr(q, ' ');
    if (p)
        *p++ = 0;
    strncpy(j->key, q, sizeof(j->key) - 1);
    j->auth = p ? (uint8_t)atoi(p) : 2;
    return 0;
}

/* Poll until the lease arrives.  30s: a slow AP plus DHCP. */
static int waitup(void)
{
    struct net_status st;
    int last = -1;
    int i;

    for (i = 0; i < 60; i++) {
        if (ioctl(sysfd, NETIOC_STATUS, &st) < 0) {
            perror("NETIOC_STATUS");
            return 1;
        }
        if (st.wifi < 0) {
            printf("%s\n", linkname(st.link, st.wifi));
            return 1;
        }
        if (st.link == 3)
            return status(0);
        if (st.link != last) {
            printf("%s\n", linkname(st.link, st.wifi));
            last = st.link;
        }
        sleep(1);
    }
    printf("timed out\n");
    return 1;
}

int main(int argc, char *argv[])
{
    struct net_join j;
    int r;

    sysfd = open("/dev/sys", O_RDWR, 0);
    if (sysfd < 0) {
        perror("/dev/sys");
        return 1;
    }

    if (argc < 2)
        return status(1);

    if (!strcmp(argv[1], "down")) {
        if (ioctl(sysfd, NETIOC_DOWN) < 0) {
            perror("NETIOC_DOWN");
            return 1;
        }
        return 0;
    }

    memset(&j, 0, sizeof(j));
    if (!strcmp(argv[1], "-f")) {
        if (readconf(argc > 2 ? argv[2] : CONF, &j))
            return 1;
    } else if (argc >= 3) {
        strncpy(j.ssid, argv[1], sizeof(j.ssid) - 1);
        strncpy(j.key, argv[2], sizeof(j.key) - 1);
        j.auth = argc > 3 ? (uint8_t)atoi(argv[3]) : 2;
    } else {
        fprintf(stderr, "usage: wifi [ssid key [auth] | -f [file] | down]\n");
        return 1;
    }

    r = ioctl(sysfd, NETIOC_UP, &j);
    if (r < 0) {
        perror("NETIOC_UP");
        return 1;
    }
    return waitup();
}

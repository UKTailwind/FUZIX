/* mmedit shim: terminal, keyboard and file I/O between MMBasic's
 * editor and Fuzix.
 *
 * The keyboard is the interesting half.  MMBasic's editor expects
 * MMInkey() to hand back one code per key - 0x91 for F1, 0x80 for UP -
 * but a terminal sends escape sequences, and it has to be a terminal:
 * the console is shared with a serial line, and TeraTerm cannot send
 * 0x91.  inkey() below reassembles them.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include "mmedit.h"

int scr_rows = 40, scr_cols = 80;

static struct termios saved;
static int raw_active;

/* --- terminal ------------------------------------------------------------- */

static void on_signal(int sig)
{
    term_close();
    _exit(128 + sig);
}

int term_open(void)
{
    struct termios t;
    struct winsize ws;

    if (tcgetattr(0, &saved) < 0)
        return -1;
    t = saved;
    /* ISIG stays ON deliberately.  MMBasic's editor binds Ctrl-Q, W
     * and Y but never Ctrl-C, so nothing is lost - and it means a
     * wedged input loop can always be broken out of, with on_signal
     * putting the terminal back.  An editor that can strand the
     * terminal is worse than an editor missing a key. */
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_iflag &= ~(ICRNL | IXON);
    t.c_oflag &= ~OPOST;
    /* VMIN 0 / VTIME 1: read returns the moment a byte arrives, or
     * empty after 100ms.  That gives inkey() its non-blocking poll AND
     * the gap that separates a bare ESC from the start of a sequence,
     * with one termios setting and no per-key syscalls. */
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 1;
    if (tcsetattr(0, TCSANOW, &t) < 0)
        return -1;
    raw_active = 1;

    /* Never leave the terminal raw if we are killed. */
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    signal(SIGQUIT, on_signal);

    if (ioctl(0, TIOCGWINSZ, &ws) == 0 && ws.ws_row && ws.ws_col) {
        scr_rows = ws.ws_row;
        scr_cols = ws.ws_col;
    }
    return 0;
}

void term_close(void)
{
    if (!raw_active)
        return;
    raw_active = 0;
    scr_wrap(1);
    scr_cursor(1);
    scr_normal();
    scr_flush();
    tcsetattr(0, TCSANOW, &saved);
}

/* --- output --------------------------------------------------------------- */
/* Buffered: a full repaint is ~3K of escapes and characters, and one
 * write beats three thousand. */
static char obuf[2048];
static int olen;

void scr_flush(void)
{
    if (olen) {
        write(1, obuf, olen);
        olen = 0;
    }
}

void scr_putc(char c)
{
    if (olen == sizeof(obuf))
        scr_flush();
    obuf[olen++] = c;
}

void scr_puts(const char *s)
{
    while (*s)
        scr_putc(*s++);
}

static void scr_num(int n)
{
    char b[8];
    int i = 0;
    if (n <= 0) {
        scr_putc('0');
        return;
    }
    while (n && i < (int)sizeof(b)) {
        b[i++] = '0' + n % 10;
        n /= 10;
    }
    while (i)
        scr_putc(b[--i]);
}

void scr_cls(void)      { scr_puts("\033[2J\033[H"); }
void scr_eol(void)      { scr_puts("\033[K"); }
void scr_normal(void)   { scr_puts("\033[0m"); }
void scr_inverse(int on) { scr_puts(on ? "\033[7m" : "\033[27m"); }
void scr_cursor(int on) { scr_puts(on ? "\033[?25h" : "\033[?25l"); }
void scr_wrap(int on)   { scr_puts(on ? "\033[?7h" : "\033[?7l"); }

void scr_goto(int row, int col)
{
    scr_puts("\033[");
    scr_num(row + 1);
    scr_putc(';');
    scr_num(col + 1);
    scr_putc('H');
}

void scr_colour(int fg, int bg)
{
    if (fg >= 0) {
        scr_puts("\033[");
        scr_num(30 + (fg & 7));
        scr_putc('m');
    }
    if (bg >= 0) {
        scr_puts("\033[");
        scr_num(40 + (bg & 7));
        scr_putc('m');
    }
}

/* --- keyboard ------------------------------------------------------------- */
/* One byte of pushback is enough: the only time we over-read is an ESC
 * followed by something that turns out not to be a sequence. */
static int pushed = -1;

static int readb(void)
{
    unsigned char c;
    if (pushed >= 0) {
        int r = pushed;
        pushed = -1;
        return r;
    }
    scr_flush();                    /* never wait on input with output pending */
    if (read(0, &c, 1) != 1)
        return -1;                  /* 100ms passed with nothing */
    return c;
}

/* CSI parameter forms we care about:
 *   ESC [ A..D H F          cursor cluster
 *   ESC [ <n> ~             editing cluster and F5-F12
 *   ESC [ <n> ; <mod> ~     the same, shifted
 *   ESC [ 1 ; <mod> P..S    shifted F1-F4
 *   ESC O P..S              F1-F4
 */
static int decode_csi(void)
{
    int n = 0, mod = 0, c;
    int have_n = 0;

    for (;;) {
        c = readb();
        if (c < 0)
            return K_ESC;           /* truncated: treat as a bare ESC */
        if (c >= '0' && c <= '9') {
            n = n * 10 + (c - '0');
            have_n = 1;
            continue;
        }
        if (c == ';') {
            mod = 0;
            for (;;) {
                c = readb();
                if (c < 0)
                    return K_ESC;
                if (c >= '0' && c <= '9') {
                    mod = mod * 10 + (c - '0');
                    continue;
                }
                break;
            }
        }
        break;
    }

    /* mod 2 = shift (xterm encodes modifier+1) */
    {
        int shifted = (mod == 2);
        int k = 0;

        switch (c) {
        case 'A': k = K_UP; break;
        case 'B': k = K_DOWN; break;
        case 'C': k = K_RIGHT; break;
        case 'D': k = K_LEFT; break;
        case 'H': k = K_HOME; break;
        case 'F': k = K_END; break;
        case 'P': k = K_F1; break;      /* CSI 1;2 P = shift-F1 */
        case 'Q': k = K_F2; break;
        case 'R': k = K_F3; break;
        case 'S': k = K_F4; break;
        case '~':
            if (!have_n)
                return 0;
            switch (n) {
            case 2:  k = K_INSERT; break;
            case 3:  k = K_DEL; break;
            case 5:  k = K_PUP; break;
            case 6:  k = K_PDOWN; break;
            case 15: k = K_F5; break;
            case 17: k = K_F6; break;
            case 18: k = K_F7; break;
            case 19: k = K_F8; break;
            case 20: k = K_F9; break;
            case 21: k = K_F10; break;
            case 23: k = K_F11; break;
            case 24: k = K_F12; break;
            default: return 0;          /* unknown: swallow, never emit junk */
            }
            break;
        default:
            return 0;
        }
        return shifted ? K_SHIFT(k) : k;
    }
}

int inkey(void)
{
    int c = readb();

    if (c < 0)
        return 0;                   /* nothing waiting */
    if (c != K_ESC)
        return c;

    c = readb();
    if (c < 0)
        return K_ESC;               /* nothing followed: a real Escape */
    if (c == '[')
        return decode_csi();
    if (c == 'O') {
        c = readb();
        switch (c) {
        case 'P': return K_F1;
        case 'Q': return K_F2;
        case 'R': return K_F3;
        case 'S': return K_F4;
        default:  return 0;
        }
    }
    /* ESC followed by something else: hand back the ESC and keep the
     * byte for next time (Alt-<key> on some terminals). */
    pushed = c;
    return K_ESC;
}

/* --- the buffer and files -------------------------------------------------- */
static unsigned char edbuf_store[EDBUF_SIZE];
unsigned char *EdBuff = edbuf_store;
int EdBuffSize = EDBUF_SIZE;
int nbrlines;

int buf_count_lines(void)
{
    unsigned char *p = EdBuff;
    int n = 0;
    while (*p) {
        if (*p++ == '\n')
            n++;
    }
    /* a trailing partial line still counts */
    if (p != EdBuff && p[-1] != '\n')
        n++;
    return n;
}

unsigned char *buf_line(int n)
{
    unsigned char *p = EdBuff;
    if (n <= 0)
        return p;
    while (*p) {
        if (*p++ == '\n' && --n == 0)
            return p;
    }
    return NULL;
}

int file_load(const char *name)
{
    int fd, r, used = 0;

    EdBuff[0] = 0;
    nbrlines = 0;

    fd = open(name, O_RDONLY);
    if (fd < 0)
        return (errno == ENOENT) ? 0 : -1;   /* a new file is not an error */

    for (;;) {
        r = read(fd, EdBuff + used, EDBUF_SIZE - 1 - used);
        if (r < 0) {
            close(fd);
            return -1;
        }
        if (r == 0)
            break;
        used += r;
        if (used >= EDBUF_SIZE - 1) {
            close(fd);
            errno = EFBIG;              /* caller reports "file too big" */
            return -1;
        }
    }
    close(fd);
    EdBuff[used] = 0;
    nbrlines = buf_count_lines();
    return used;
}

int file_save(const char *name)
{
    int fd, n, w, off = 0;

    fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -1;
    n = strlen((char *)EdBuff);
    while (off < n) {
        w = write(fd, EdBuff + off, n - off);
        if (w <= 0) {
            close(fd);
            return -1;
        }
        off += w;
    }
    return close(fd);
}

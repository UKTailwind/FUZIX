/* keydump - show the bytes a key actually sends.
 *
 * Raw mode, no buffering, one line per key: what arrived, as hex and
 * as printable text.  Quit with q on its own (or Ctrl-C).
 *
 * Use it to check the keyboard against what the pc3 termcap entry
 * claims, e.g. F1 should be ESC O P and the up arrow ESC [ A.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>

static struct termios saved;

static void raw_on(void)
{
    struct termios t;
    tcgetattr(0, &saved);
    t = saved;
    t.c_lflag &= ~(ICANON | ECHO);
    /* Return whatever has arrived after a short gap, so a multi-byte
     * escape sequence is shown as ONE key rather than split, and a
     * bare Escape still comes through. */
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 1;          /* 100ms */
    tcsetattr(0, TCSANOW, &t);
}

int main(void)
{
    unsigned char buf[16];
    int n, i;

    printf("keydump: press keys, 'q' to quit.\n");
    fflush(stdout);
    raw_on();

    for (;;) {
        n = read(0, buf, sizeof(buf));
        if (n <= 0)
            continue;
        if (n == 1 && buf[0] == 'q')
            break;

        printf("%d byte%s:", n, n == 1 ? "" : "s");
        for (i = 0; i < n; i++)
            printf(" %02x", buf[i]);
        printf("   ");
        for (i = 0; i < n; i++) {
            if (buf[i] == 27)
                printf("ESC ");
            else if (buf[i] >= 32 && buf[i] < 127)
                printf("%c", buf[i]);
            else
                printf("<%d>", buf[i]);
        }
        printf("\r\n");
        fflush(stdout);
    }

    tcsetattr(0, TCSANOW, &saved);
    printf("\n");
    return 0;
}

#include <unistd.h>
#include <termios.h>
#include <stdio.h>
#include "debug.h"
#include "term_ansi.h"
#include "errno.h"

static struct termios old_termios;
/* static int raw_mode = 0; */

void term_cls(void)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");
    printf("\033[0m");
    printf("\033[2J\033[H");
    fflush(stdout);
}

void term_set_cursor(int y, int x)
{
    debug_log(DEBUG_TRACE, FUNC_NAME, "Enter:");
    printf("\033[%d;%dH", y, x);
    fflush(stdout);
}

void term_puts(const char *str)
{
    printf("%s", str);
    fflush(stdout);
}

void term_save_cursor(void)
{
    printf("\033[s");
    fflush(stdout);
}

void term_restore_cursor(void)
{
    printf("\033[u");
    fflush(stdout);
}

void term_reverse_on(void)
{
    printf("\033[7m");
    fflush(stdout);
}

void term_reverse_off(void)
{
    printf("\033[27m");
    fflush(stdout);
}

void term_raw_on(void)
{
    struct termios t;
    int rc;

    /* save current terminal settings */
    rc = tcgetattr(STDIN_FILENO, &old_termios);
    if (rc != 0) {
        debug_log(DEBUG_ERROR, FUNC_NAME, "tcgetattr failed errno=%d", errno);
        return;
    }

    memcpy(&t, &old_termios, sizeof(struct termios));

    /* disable canonical mode and echo; keep ISIG enabled for Ctrl+C/Ctrl+X */
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN]  = 1;    /* deliver each key immediately */
    t.c_cc[VTIME] = 0;    /* no timeout */

    rc = tcsetattr(STDIN_FILENO, TCSANOW, &t);
    if (rc != 0) {
        debug_log(DEBUG_ERROR, FUNC_NAME, "tcsetattr failed errno=%d", errno);
    } else {
        debug_log(DEBUG_TRACE, FUNC_NAME, "Terminal raw mode enabled");
    }
}

void term_raw_off(void)
{
    tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
}

/* Set time out for non blocking reads */
void term_set_vmin_vtime(int vmin, int vtime)
{
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    t.c_cc[VMIN]  = vmin;
    t.c_cc[VTIME] = vtime;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

/* wraptest - check the console's last-column (deferred wrap) behaviour.
 *
 * A VT100 does not move the cursor when the last column is written: it
 * stays there and the wrap happens when the NEXT printable character
 * arrives.  Getting this wrong makes every full-screen program come out
 * a line adrift once it paints column 80.
 *
 * The console answers CSI 6n, so this asks it where the cursor really
 * is rather than relying on anyone looking at the screen.  Run it on
 * the PC3 console (it needs the console's own DSR reply; over a plain
 * serial terminal the terminal answers instead, which is also a valid
 * thing to test).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>

static struct termios saved;
static int fails;

static void raw_on(void)
{
    struct termios t;
    tcgetattr(0, &saved);
    t = saved;
    t.c_lflag &= ~(ICANON | ECHO);
    /* Never block: if nothing answers CSI 6n this must fail the test,
     * not wedge the shell on a machine with no way to interrupt it. */
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 5;          /* half a second */
    tcsetattr(0, TCSANOW, &t);
}

static void raw_off(void)
{
    tcsetattr(0, TCSANOW, &saved);
}

static void put(const char *s)
{
    write(1, s, strlen(s));
}

/* Ask the console for the cursor position: CSI 6n -> ESC [ row ; col R */
static int where(int *row, int *col)
{
    char buf[32];
    int n = 0;
    char c;
    char *p;

    *row = *col = -1;
    put("\033[6n");
    while (n < (int)sizeof(buf) - 1) {
        if (read(0, &c, 1) != 1)
            return -1;                  /* timed out: nobody answered */
        buf[n++] = c;
        if (c == 'R')
            break;
    }
    buf[n] = 0;
    if (n < 6 || buf[0] != 033 || buf[1] != '[')
        return -1;
    p = strchr(buf, ';');
    if (!p)
        return -1;
    *row = atoi(buf + 2);
    *col = atoi(p + 1);
    return 0;
}

static void check(const char *what, int gotr, int gotc, int wantr, int wantc)
{
    int ok = (gotr == wantr && gotc == wantc);
    if (!ok)
        fails++;
    /* Report on the serial side only once the screen test is done, so
     * the printf does not itself disturb the cursor mid-test. */
    fprintf(stderr, "  %-42s (%d,%d) want (%d,%d)  %s\n",
            what, gotr, gotc, wantr, wantc, ok ? "ok" : "FAIL");
}

int main(void)
{
    int r, c;
    struct {
        int r, c;
        char what[48];
    } res[8];
    int n = 0;

    raw_on();
    put("\033[2J");

    /* 1. write the last column: the cursor must STAY on column 80 */
    put("\033[1;80H");
    put("A");
    where(&r, &c);
    res[n].r = r; res[n].c = c;
    strcpy(res[n++].what, "after writing col 80, cursor stays");

    /* 2. the next printable character takes the deferred wrap */
    put("B");
    where(&r, &c);
    res[n].r = r; res[n].c = c;
    strcpy(res[n++].what, "next char wraps to row 2 col 2");

    /* 3. SGR must NOT cancel a pending wrap (the editor recolours at
     *    the right margin all the time) */
    put("\033[2;80HC");
    put("\033[31m");
    put("D");
    where(&r, &c);
    res[n].r = r; res[n].c = c;
    strcpy(res[n++].what, "SGR does not cancel the pending wrap");
    put("\033[0m");

    /* 4. a cursor move DOES cancel it */
    put("\033[4;80HE");
    put("\033[4;1H");
    put("F");
    where(&r, &c);
    res[n].r = r; res[n].c = c;
    strcpy(res[n++].what, "cursor move cancels the pending wrap");

    /* 5. autowrap off: the cursor sticks and overwrites */
    put("\033[?7l");
    put("\033[6;80HG");
    put("H");
    where(&r, &c);
    res[n].r = r; res[n].c = c;
    strcpy(res[n++].what, "DECAWM off: no wrap, stays on col 80");
    put("\033[?7h");

    /* 6. autowrap back on behaves as (1) again */
    put("\033[8;80HI");
    where(&r, &c);
    res[n].r = r; res[n].c = c;
    strcpy(res[n++].what, "DECAWM back on: defers again");

    /* 7. the bottom right cell must not scroll the screen: write it,
     *    then check we are still on the last row */
    put("\033[40;80HJ");
    where(&r, &c);
    res[n].r = r; res[n].c = c;
    strcpy(res[n++].what, "bottom-right cell does not scroll");

    put("\033[2J\033[H");
    raw_off();

    check(res[0].what, res[0].r, res[0].c, 1, 80);
    check(res[1].what, res[1].r, res[1].c, 2, 2);
    check(res[2].what, res[2].r, res[2].c, 3, 2);
    check(res[3].what, res[3].r, res[3].c, 4, 2);
    check(res[4].what, res[4].r, res[4].c, 6, 80);
    check(res[5].what, res[5].r, res[5].c, 8, 80);
    check(res[6].what, res[6].r, res[6].c, 40, 80);

    fprintf(stderr, "%s\n", fails ? "WRAP TEST FAILED" : "wrap test passed");
    return fails ? 1 : 0;
}

/* mmedit - stage 3: the shim proven end to end.
 *
 * Loads a file into the flat 120K buffer, paints it, and navigates it.
 * No editing yet - this exists to prove the pieces the ported MMBasic
 * editor will stand on: raw mode that is always restored, inkey()
 * turning VT100 sequences back into MMBasic's key codes, the buffer,
 * and a full-width repaint that does not fight the console.
 *
 * The status line reports the last key code, so the keyboard can be
 * checked against MMBasic's table (F1 = 0x91, UP = 0x80, ...) from
 * inside the program that will consume them.
 *
 *   mmedit <file>      arrows / PgUp / PgDn / Home / End to move
 *                      F1 or ESC to leave
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "mmedit.h"

static const char *fname;
static int edy;                 /* first buffer line shown */
static int cury, curx;          /* cursor, relative to the window */
static int lastkey;
static int textrows;

static int line_len(int n)
{
    unsigned char *p = buf_line(n);
    int len = 0;
    if (!p)
        return 0;
    while (p[len] && p[len] != '\n')
        len++;
    return len;
}

static void paint_line(int row, int lineno)
{
    unsigned char *p = buf_line(lineno);
    int col = 0;

    scr_goto(row, 0);
    if (p) {
        while (*p && *p != '\n' && col < scr_cols) {
            /* Tabs to the next multiple of 8, spaces on the screen so
             * the column arithmetic stays honest. */
            if (*p == '\t') {
                do {
                    scr_putc(' ');
                    col++;
                } while ((col & 7) && col < scr_cols);
                p++;
            } else if (*p < 32) {
                p++;                    /* control junk: skip */
            } else {
                scr_putc(*p++);
                col++;
            }
        }
    }
    if (col < scr_cols)
        scr_eol();
}

static void paint_status(void)
{
    char buf[96];
    int n;

    scr_goto(scr_rows - 1, 0);
    scr_inverse(1);
    n = snprintf(buf, sizeof(buf),
                 " %-20s  %d lines  line %d col %d   key %02X   F1/ESC quit",
                 fname, nbrlines, edy + cury + 1, curx + 1, lastkey);
    if (n > scr_cols)
        n = scr_cols;
    buf[n] = 0;
    scr_puts(buf);
    /* Pad rather than erase-to-end: with the cursor on the last column
     * of the last row, an erase is fine but a written character is the
     * case that used to scroll the screen.  Write it deliberately. */
    while (n < scr_cols) {
        scr_putc(' ');
        n++;
    }
    scr_inverse(0);
    scr_normal();
}

static void paint(void)
{
    int r;

    scr_cursor(0);
    for (r = 0; r < textrows; r++)
        paint_line(r, edy + r);
    paint_status();
    scr_goto(cury, curx);
    scr_cursor(1);
    scr_flush();
}

static void clamp(void)
{
    int len;

    if (edy + cury >= nbrlines) {
        cury = nbrlines - 1 - edy;
        if (cury < 0) {
            cury = 0;
            edy = nbrlines ? nbrlines - 1 : 0;
        }
    }
    if (cury < 0)
        cury = 0;
    if (cury >= textrows)
        cury = textrows - 1;
    len = line_len(edy + cury);
    if (curx > len)
        curx = len;
    if (curx < 0)
        curx = 0;
    if (curx >= scr_cols)
        curx = scr_cols - 1;
}

int main(int argc, char *argv[])
{
    int key, n, dirty;

    if (argc != 2) {
        fprintf(stderr, "usage: mmedit <file>\n");
        return 1;
    }
    fname = argv[1];

    n = file_load(fname);
    if (n < 0) {
        if (errno == EFBIG)
            fprintf(stderr, "mmedit: %s is larger than %d bytes\n",
                    fname, EDBUF_SIZE - 1);
        else
            perror(fname);
        return 1;
    }

    if (term_open() < 0) {
        fprintf(stderr, "mmedit: not a terminal\n");
        return 1;
    }
    textrows = scr_rows - 1;
    scr_wrap(0);                /* as MMBasic's editor does */
    scr_cls();
    paint();

    for (;;) {
        key = inkey();
        if (key == 0)
            continue;
        lastkey = key;
        dirty = 1;

        switch (key) {
        case K_F1:
        case K_ESC:
        case CTRLKEY('Q'):
            goto done;

        case K_UP:
            if (cury > 0)
                cury--;
            else if (edy > 0)
                edy--;
            break;
        case K_DOWN:
            if (edy + cury + 1 < nbrlines) {
                if (cury < textrows - 1)
                    cury++;
                else
                    edy++;
            }
            break;
        case K_LEFT:
            if (curx > 0)
                curx--;
            break;
        case K_RIGHT:
            if (curx < line_len(edy + cury))
                curx++;
            break;
        case K_HOME:
            curx = 0;
            break;
        case K_END:
            curx = line_len(edy + cury);
            break;
        case K_PUP:
            edy -= textrows;
            if (edy < 0)
                edy = 0;
            break;
        case K_PDOWN:
            if (edy + textrows < nbrlines)
                edy += textrows;
            break;
        default:
            break;              /* shown in the status line, ignored */
        }

        if (dirty) {
            clamp();
            paint();
        }
    }

done:
    scr_cls();
    term_close();
    printf("mmedit: %s, %d lines, %d bytes\n", fname, nbrlines, n);
    return 0;
}

/* mmedit - MMBasic's full screen editor as a Fuzix file editor.
 *
 * This file is only the wrapper: load the file, set up the terminal,
 * hand over to FullScreenEditor, and put the terminal back afterwards
 * whatever happens.  The editor itself is in editor.c.
 *
 *   mmedit <file>      F1 save and exit, ESC abandon, F3 find,
 *                      F9 import, F10 export
 *
 * A .bas file is colour coded three ways: cyan for a keyword mmbc can
 * translate, blue for one only the interpreter knows, and the usual
 * yellow/green/magenta for comments, numbers and strings.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "mmedit.h"

int main(int argc, char *argv[])
{
    int n;

    if (argc != 2) {
        fprintf(stderr, "usage: mmedit <file>\n");
        return 1;
    }

    n = file_load(argv[1]);
    if (n < 0) {
        if (errno == EFBIG)
            fprintf(stderr, "mmedit: %s is larger than %d bytes\n",
                    argv[1], EDBUF_SIZE - 1);
        else
            perror(argv[1]);
        return 1;
    }

    if (term_open() < 0) {
        fprintf(stderr, "mmedit: not a terminal\n");
        return 1;
    }

    /* The editor owns the screen bar its two status lines, and it wants
     * autowrap off - painting the last column of a row must not move the
     * cursor.  The console honours DECAWM now (see PC3-EDITOR-REVIEW.md);
     * before that fix no full-screen program could work here at all. */
    VWidth = scr_cols;
    VHeight = scr_rows - 2;
    scr_wrap(0);
    scr_cls();
    scr_flush();

    txtp = EdBuff;
    FullScreenEditor(0, 0, argv[1], EDBUF_SIZE, 0);

    scr_flush();
    term_close();

    /* F2 is "save, exit and run" on a PicoMite.  There is no interpreter
     * to hand the program to here, so say so rather than pretend. */
    if (editor_exit_key == K_F2)
        printf("mmedit: saved.  To run it: mmbc %s; cc %.*s.c; ./%.*s.bc\n",
               argv[1], (int)(strlen(argv[1]) - 4), argv[1],
               (int)(strlen(argv[1]) - 4), argv[1]);
    return 0;
}

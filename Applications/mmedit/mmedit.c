/* mmedit - MMBasic's full screen editor as a Fuzix file editor.
 *
 * This file is only the wrapper: load the file, set up the terminal,
 * hand over to FullScreenEditor, and put the terminal back afterwards
 * whatever happens.  The editor itself is in editor.c.
 *
 *   mmedit <file>      F1 save and exit, F2 save, compile and run,
 *                      ESC abandon, F3 find, F9 import, F10 export
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
#ifdef PC3_HOST
#include "pc3client.h"
#endif

#define CC_CMD  "/usr/bin/cc"

/* The extensions cc can be handed: .bas goes through mmbc first, .c
 * straight into the pipeline, and both are spelt either way because a
 * file that came off a PC is as likely to be .BAS as .bas. */
static int is_source(const char *name)
{
    const char *slash = strrchr(name, '/');
    const char *dot = strrchr(slash ? slash + 1 : name, '.');

    if (dot == NULL)
        return 0;
    return strcmp(dot, ".bas") == 0 || strcmp(dot, ".BAS") == 0 ||
           strcmp(dot, ".c") == 0   || strcmp(dot, ".C") == 0;
}

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

    /* F2 is "save, exit and run" on a PicoMite, where the interpreter
     * took the program straight back.  Here it is cc -r: the file is
     * built - a .bas through mmbc first, cc knows both dialects - and
     * the result is run if it built, which is as close to F2 as a
     * compiled machine gets.  A compile error stops there, with the
     * errors on screen.  Ctrl-W is F2's alias in MMBasic's own
     * dispatch, so it does the same.
     *
     * execv, not fork: the editor has nothing left to do, and 200K of
     * editor waiting in waitpid() is most of the process pool that the
     * compile - and then the program - is about to want.  cc execs the
     * program itself for the same reason.  The terminal is already back
     * in cooked mode above, so both inherit a sane tty. */
    if (editor_exit_key == K_F2 || editor_exit_key == CTRLKEY('W')) {
        char *av[4];

        if (!is_source(argv[1])) {
            printf("mmedit: saved.  %s is not .bas or .c, so there is "
                   "nothing to compile\n", argv[1]);
            return 0;
        }
#ifdef PC3_HOST
        /* The cc beside this program, wherever that is: /usr/bin/cc on
           a PC is the system's compiler.  MMEDIT_CC names another -
           a wrapper, a different build, a script that records what it
           was asked - and is run the same way, with -r and the file. */
        {
            static char hcc[4200];
            char dir[4096];
            const char *cc = CC_CMD;
            const char *env = getenv("MMEDIT_CC");
            const char *slash;

            if (env && *env)
                cc = env;
            else if (pc3_exe_dir(dir, sizeof dir) == 0) {
                snprintf(hcc, sizeof hcc, "%s/cc", dir);
                cc = hcc;
            }
            slash = strrchr(cc, '/');
            printf("%s -r %s\n", slash ? slash + 1 : cc, argv[1]);
            fflush(stdout);
            av[0] = (char *) cc;
            av[1] = "-r";
            av[2] = argv[1];
            av[3] = NULL;
            execv(cc, av);
            perror(cc);
            return 1;
        }
#else
        printf("cc -r %s\n", argv[1]);
        fflush(stdout);                 /* exec does not flush for us */
        av[0] = (char *) CC_CMD;
        av[1] = "-r";                   /* build it, then run it */
        av[2] = argv[1];
        av[3] = NULL;
        execv(CC_CMD, av);
        perror(CC_CMD);
        return 1;
#endif
    }
    return 0;
}

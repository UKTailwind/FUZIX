/* mmb_compat.h - the names MMBasic's Editor.c reaches for, provided so
 * that editor.c can stay as close to the original as possible.
 *
 * The port's whole strategy is here: rather than rewrite the editor,
 * give it the handful of things it references and let the logic through
 * unchanged.  Editor.c names only MMInkey, MMgetchar, GetMemory, error,
 * routinechecks, the keyword tables and a display layer - everything
 * else it does is plain C over a flat text buffer.
 *
 * The MX470 half of that display layer is MMBasic's second output path,
 * for an LCD panel used as a console.  We have exactly one output, the
 * terminal, so those become no-ops and the VT100 path - which Editor.c
 * already had - becomes the only path.
 */

#ifndef MMB_COMPAT_H
#define MMB_COMPAT_H

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include "mmedit.h"

/* --- key codes, under MMBasic's own names -------------------------------- */
#define ESC         K_ESC
#define TAB         0x09
#define BKSP        0x08
#define DEL         K_DEL
#define UP          K_UP
#define DOWN        K_DOWN
#define LEFT        K_LEFT
#define RIGHT       K_RIGHT
#define INSERT      K_INSERT
#define HOME        K_HOME
#define END         K_END
#define PUP         K_PUP
#define PDOWN       K_PDOWN
#define F1          K_F1
#define F2          K_F2
#define F3          K_F3
#define F4          K_F4
#define F5          K_F5
#define F6          K_F6
#define F7          K_F7
#define F8          K_F8
#define F9          K_F9
#define F10         K_F10
#define F11         K_F11
#define F12         K_F12
#define SHIFT_F3    K_SHIFT_F3
#define SHIFT_F4    K_SHIFT_F4
#define SHIFT_F5    K_SHIFT_F5

/* MMBasic builds as C99 with stdbool; the Fuzix cross toolchain is
 * gnu89 here, and the editor only ever uses these three names. */
#ifndef __cplusplus
typedef int bool_t;
#ifndef true
#define true  1
#define false 0
#endif
#endif

/* --- sizes ---------------------------------------------------------------- */
#define STRINGSIZE  256
#define MAXSTRLEN   255
#define MAXCLIP     1024

/* --- MMBasic's small helpers ---------------------------------------------- */
#define mytoupper(c)  toupper((unsigned char)(c))
#define isnamechar(c) (isalnum((unsigned char)(c)) || (c) == '_' || (c) == '.')
#define skipspace(x)  while (*x == ' ') x++

/* Compare two areas of memory ignoring case - MMBasic's own, verbatim. */
static inline int mem_equal(unsigned char *s1, unsigned char *s2, int i)
{
    if (mytoupper(*s1) != mytoupper(*s2))
        return 0;
    while (--i) {
        if (mytoupper(*++s1) != mytoupper(*++s2))
            return 0;
    }
    return 1;
}

static inline void IntToStr(char *s, int n, int base)
{
    sprintf(s, (base == 16) ? "%x" : "%d", n);
}

/* --- the options the editor actually consults ----------------------------- */
/* A struct so the source keeps saying Option.ColourCode.  DISPLAY_CONSOLE
 * is the MX470 panel, which we do not have; continuation is MMBasic's
 * line-continuation character, off for ordinary text files. */
struct mmb_option {
    int ColourCode;
    int Tab;
    int DISPLAY_CONSOLE;
    int continuation;
};
extern struct mmb_option Option;

/* --- the MX470 panel: not present ---------------------------------------- */
/* Every one of these had a live counterpart on a PicoMite with an LCD
 * console.  Here the terminal is the only output, so they compile away -
 * which is also what removes the tile-colour machinery wholesale. */
#define MX470PutC(c)            ((void)0)
#define MX470PutS(s, fc, bc)    ((void)0)
#define MX470Cursor(x, y)       ((void)0)
#define MX470Display(fn)        ((void)0)
#define MX470Scroll(n)          ((void)0)
#define ClearScreen(c)          ((void)0)
#define ShowCursor(on)          scr_cursor(on)

#define DISPLAY_CLS     1
#define REVERSE_VIDEO   3
#define CLEAR_TO_EOL    4
#define CLEAR_TO_EOS    5
#define SCROLL_DOWN     6
#define DRAW_LINE       7
#define SCROLLCHARS     3

/* The colours are indexes into nothing now - only the VT100 strings are
 * used - but SetColour tracks gui_fcolour to decide what to re-emit
 * after skipping off-screen characters, so they must stay distinct. */
#define GUI_C_NORMAL    7
#define GUI_C_BCOLOUR   0
#define GUI_C_COMMENT   3
#define GUI_C_KEYWORD   6
#define GUI_C_QUOTE     5
#define GUI_C_NUMBER    2
#define GUI_C_LINE      5
#define GUI_C_STATUS    7
extern int gui_fcolour, gui_bcolour;

#define VT100_C_NORMAL  "\033[37m"
#define VT100_C_COMMENT "\033[33m"
#define VT100_C_KEYWORD "\033[36m"
#define VT100_C_QUOTE   "\033[35m"
#define VT100_C_NUMBER  "\033[32m"
#define VT100_C_LINE    "\033[35m"
#define VT100_C_STATUS  "\033[37m"
#define VT100_C_ERROR   "\033[31m"

/* Three colours for keywords, which was the open question in the review:
 * everything MMBasic knows is a keyword, but what mmbc can actually
 * translate is cyan and what it cannot is blue.  A program that is all
 * cyan will compile. */
#define VT100_C_KEYWORD_UNSUP "\033[34m"
#define GUI_C_KEYWORD_UNSUP   4

/* --- output --------------------------------------------------------------- */
/* Editor.c calls these through function pointers so it can be aimed at a
 * serial console or the panel.  There is only one target here. */
void PrintString(char *s);
char SSputchar(char c, int flush);

/* --- input ---------------------------------------------------------------- */
/* MMInkey returns -1 when nothing is waiting; inkey() returns 0, because
 * 0 is not a key.  getConsole is the drain used after a repaint and must
 * NOT wait - a 100ms stall on every redraw is visible. */
int MMInkey(void);
int MMgetchar(void);
int getConsole(void);
void routinechecks(void);

/* --- keyword tables ------------------------------------------------------- */
/* MMBasic's real tables carry the parser's function pointers; the editor
 * only ever reads .name, so this is the whole of what it needs. */
struct mmb_keyword {
    const char *name;
    unsigned char supported;    /* mmbc can translate it */
};
extern const struct mmb_keyword commandtbl[];
extern const struct mmb_keyword tokentbl[];
extern const int CommandTableSize;      /* MMBasic's are size+1 - see below */
extern const int TokenTableSize;
extern const struct mmb_keyword overlaid_functions[];
extern const struct mmb_keyword hidden_functions[];
extern const struct mmb_keyword twokeyword_tbl[];
extern const struct mmb_keyword special_keywords[];
extern const int MMEND;

#endif

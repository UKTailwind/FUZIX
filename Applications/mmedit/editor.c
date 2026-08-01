/* editor.c - MMBasic's full screen editor, ported to Fuzix.
 *
 * Taken from PicoMite 6.03.00 Editor.c: FullScreenEditor and the utility
 * functions under it, plus findLine/SetColour/printLine/printScreen.
 * The file manager, which is more than half of that file, is out of
 * scope - this edits one file.
 *
 * The port is deliberately shallow.  Editor.c was written with two
 * output backends, one for an LCD panel used as a console and one for a
 * serial terminal, and the serial one emits ordinary ANSI.  We keep that
 * path and compile the panel away (mmb_compat.h), so there is no
 * renderer to write and the editing logic arrives unchanged.
 *
 * What was changed, and why:
 *
 *   - the mouse.  Two blocks, one for a USB mouse over the tile display
 *     and one decoding TeraTerm's mouse reports out of the ESC handler.
 *     The first has no hardware here.  The second CANNOT work here: our
 *     inkey() reassembles escape sequences itself, so the editor never
 *     sees a bare ESC followed by '[', and leaving the code in would
 *     have made ESC unreliable rather than merely dead.
 *   - saving.  MMBasic writes through its own file layer and, for the
 *     in-memory program, into flash.  Here it is open/write/close plus a
 *     .bak, and CRLF becomes LF because this is a Unix filesystem.
 *   - beautify (F12) and mark mode (F4) are not ported yet; both are
 *     self-contained and announce themselves.
 *   - the third keyword colour, which is new: see keywords.c.
 *
 * Everything else - the key dispatch, the type-ahead buffer trick that
 * makes BKSP-at-column-0 into UP END DEL, the scroll arithmetic, the
 * colour state machine - is MMBasic's, and the comments with it are its
 * authors'.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "mmedit.h"
#include "mmb_compat.h"

/* --- the globals Editor.c works through ---------------------------------- */
int VWidth, VHeight;    /* editing area, in characters (VHeight excludes
                         * the two status lines) */
int edx, edy;           /* text column and line at the top left corner */
int curx, cury;         /* cursor, relative to that corner */
unsigned char *txtp;    /* the cursor's position in the text itself */
int drawstatusline;
int insert;
int tempx;              /* preferred column, tracked across up/down */
int TextChanged;
int gui_fcolour = GUI_C_NORMAL, gui_bcolour = GUI_C_BCOLOUR;
int editor_exit_key;

struct mmb_option Option = { 1, 4, 0, 0 };

static bool_t edit_is_bas;
static int multilinecomment = false;
static unsigned char inpbuf[STRINGSIZE];
static unsigned char tknbuf[STRINGSIZE];

#define EDIT 1          /* which status line legend to draw */
#define MARK 2

char *findLine(int ln, int *inmulti);
void SetColour(unsigned char *p, int DoVT100);
void restoreColourFromLineStart(unsigned char *target);
void printLine(int ln);
void printScreen(void);
void SCursor(int x, int y);
int editInsertChar(unsigned char c, char *multi, int edit_buff_size);
void PrintFunctKeys(int);
void PrintStatus(void);
void editDisplayMsg(unsigned char *msg);
void GetInputString(unsigned char *prompt);
void Scroll(void);
void ScrollDown(void);
void MarkMode(unsigned char *cb, unsigned char *buf);
void PositionCursor(unsigned char *curp);
int find_longest_line_length(const char *text, int *linein);

char *findLine(int ln, int *inmulti)
{
    unsigned char *p, *q;
    *inmulti = false;
    int inquote = false;
    int incomment = false;
    p = q = EdBuff;
    skipspace(q);
    if (q[0] == '/' && q[1] == '*')
        *inmulti = true;
    if (q[0] == '*' && q[1] == '/')
        *inmulti = false;
    while (ln && *p)
    {
        if (*p == '\n')
        {
            // Check if this line continues to the next via continuation character
            if (Option.continuation && p >= EdBuff + 2 &&
                *(p - 1) == Option.continuation && *(p - 2) == ' ')
            {
                // Continuation line - carry quote/comment state to next line
            }
            else
            {
                // Not a continuation - reset quote/comment state
                inquote = false;
                incomment = false;
            }
            if (*inmulti == 2)
                *inmulti = false;
            ln--;
            q = &p[1];
            skipspace(q);
            if (q[0] == '/' && q[1] == '*')
                *inmulti = true;
            if (q[0] == '*' && q[1] == '/')
                *inmulti = 2;
        }
        else if (!*inmulti)
        {
            // Track quote and comment state through the text
            if (*p == '\'' && !inquote)
                incomment = true;
            else if (*p == '"' && !incomment)
                inquote = !inquote;
        }
        p++;
    }
    // Report continuation colour state via inmulti
    if (!*inmulti && inquote)
        *inmulti = 3;
    else if (!*inmulti && incomment)
        *inmulti = 4;
    return (char *)p;
}

int EditCompStr(char *p, char *tkn)
{
    while (*tkn && (mytoupper(*tkn) == mytoupper(*p)))
    {
        if (*tkn == '(' && *p == '(')
            return true;
        if (*tkn == '$' && *p == '$')
            return true;
        tkn++;
        p++;
    }
    if (*tkn == 0 && !isnamechar((unsigned char)*p))
        return true; // return the string if successful

    return false; // or NULL if not
}

/* A keyword, in one of two colours - this is the one addition to
 * MMBasic's colour scheme.  Cyan is a keyword mmbc can translate, blue
 * is one only the interpreter knows, and the distinction comes out of
 * the translator's own tables (keywords.c).  On a machine where a BASIC
 * program is compiled rather than interpreted, "will this build?" is
 * worth knowing while you type it. */
static void keyword_colour(int supported, int DoVT100)
{
    gui_fcolour = supported ? GUI_C_KEYWORD : GUI_C_KEYWORD_UNSUP;
    if (DoVT100)
        PrintString(supported ? VT100_C_KEYWORD : VT100_C_KEYWORD_UNSUP);
}

// this function does the syntax colour coding
// p = pointer to the current character to be printed
//     or NULL if the colour coding is to be cmdfile to normal
//
// it keeps track of where it is in the line using static variables
// so it must be fed all chars from the start of the line
void SetColour(unsigned char *p, int DoVT100)
{
    int i;
    unsigned char **pp;
    static int intext = false;
    static int incomment = false;
    static int inkeyword = false;
    static unsigned char *twokeyword = NULL;
    static int inquote = false;
    static int innumber = false;

    if (!Option.ColourCode)
        return;

    // this is a list of keywords that can come after the OPTION and GUI commands
    // the list must be terminated with a NULL
    char *twokeywordtbl[] = {
        "BASE", "EXPLICIT", "DEFAULT", "BREAK", "AUTORUN", "BAUDRATE", "DISPLAY",
#if defined(GUICONTROLS)
        "BUTTON", "SWITCH", "CHECKBOX", "RADIO", "LED", "FRAME", "NUMBERBOX", "SPINBOX", "TEXTBOX", "DISPLAYBOX", "CAPTION", "DELETE",
        "DISABLE", "HIDE", "ENABLE", "SHOW", "FCOLOUR", "BCOLOUR", "REDRAW", "BEEP", "INTERRUPT",
#endif
        NULL};

    // this is a list of common keywords that should be highlighted as such
    // the list must be terminated with a NULL
    char *specialkeywords[] = {
        "SELECT", "INTEGER", "FLOAT", "STRING", "DISPLAY", "SDCARD", "OUTPUT", "APPEND", "WRITE", "SLAVE", "TARGET", "PROGRAM",
        //        ".PROGRAM", ".END PROGRAM", ".SIDE", ".LABEL" , ".LINE",".WRAP", ".WRAP TARGET",
        NULL};

    // these functions are rewritten to other tokens during tokenise() (see MMBasic.c)
    // so they no longer have their own entry in the command or token tables and would
    // otherwise not be colour coded.  The MM.xxx functions handled by fun_tilde() are
    // dealt with separately below using the overlaid_functions[] table.
    // the list must be terminated with a NULL
    char *hiddenfunctions[] = {
        "BIN$(", "OCT$(", "HEX$(", "LCASE$(", "UCASE$(", "LEFT$(", "RIGHT$(", "MIN(", "MAX(", "MM.INFO$(",
        NULL};

    // cmdfile everything back to normal
    if (p == NULL)
    {
        innumber = inquote = inkeyword = incomment = intext = false;
        twokeyword = NULL;
        if (!multilinecomment)
        {
            gui_fcolour = GUI_C_NORMAL;
            if (DoVT100)
                PrintString(VT100_C_NORMAL);
        }
        return;
    }

    // Special init for continuation line colour state
    if (p == (unsigned char *)1)
    {
        // Init in quote mode (for continuation lines split inside a string)
        inquote = true;
        gui_fcolour = GUI_C_QUOTE;
        if (DoVT100)
            PrintString(VT100_C_QUOTE);
        return;
    }
    if (p == (unsigned char *)2)
    {
        // Init in comment mode (for continuation lines split inside a comment)
        incomment = true;
        gui_fcolour = GUI_C_COMMENT;
        if (DoVT100)
            PrintString(VT100_C_COMMENT);
        return;
    }

    if (*p == '*' && p[1] == '/' && !inquote)
    {
        multilinecomment = 2;
        return;
    }

    if (*p == '/' && !inquote && multilinecomment == 2)
    {
        multilinecomment = false;
        return;
    }

    // check for a comment char
    if (*p == '\'' && !inquote)
    {
        gui_fcolour = GUI_C_COMMENT;
        if (DoVT100)
            PrintString(VT100_C_COMMENT);
        incomment = true;
        return;
    }
    if (*p == '/' && p[1] == '*' && !inquote)
    {
        if (p == EdBuff || p[-1] == (unsigned char)'\n')
        {
            gui_fcolour = GUI_C_COMMENT;
            if (DoVT100)
                PrintString(VT100_C_COMMENT);
            multilinecomment = true;
        }
        return;
    }

    // once in a comment all following chars must be comments also
    if (incomment || multilinecomment)
        return;

    // check for a quoted string
    if (*p == '\"')
    {
        if (!inquote)
        {
            inquote = true;
            gui_fcolour = GUI_C_QUOTE;
            if (DoVT100)
                PrintString(VT100_C_QUOTE);
            return;
        }
        else
        {
            inquote = false;
            return;
        }
    }

    if (inquote)
        return;

    // if we are displaying a keyword check that it is still actually in the keyword and cmdfile if not
    if (inkeyword)
    {
        if (isnamechar(*p) || *p == '$')
            return;
        gui_fcolour = GUI_C_NORMAL;
        if (DoVT100)
            PrintString(VT100_C_NORMAL);
        inkeyword = false;
        return;
    }

    // if we are displaying a number check that we are still actually in it and cmdfile if not
    // this is complicated because numbers can be in hex or scientific notation
    if (innumber)
    {
        if (!isdigit(*p) && !(toupper(*p) >= 'A' && toupper(*p) <= 'F') && toupper(*p) != 'O' && toupper(*p) != 'H' && *p != '.')
        {
            gui_fcolour = GUI_C_NORMAL;
            if (DoVT100)
                PrintString(VT100_C_NORMAL);
            innumber = false;
            return;
        }
        else
        {
            return;
        }
        // check if we are starting a number
    }
    else if (!intext)
    {
        if (isdigit(*p) || *p == '&' || ((*p == '-' || *p == '+' || *p == '.') && isdigit(p[1])))
        {
            gui_fcolour = GUI_C_NUMBER;
            if (DoVT100)
                PrintString(VT100_C_NUMBER);
            innumber = true;
            return;
        }
        // check if this is an 8 digit hex number as used in CFunctions
        for (i = 0; i < 8; i++)
            if (!isxdigit(p[i]))
                break;
        if (i == 8 && (p[8] == ' ' || p[8] == '\'' || p[8] == 0))
        {
            gui_fcolour = GUI_C_NUMBER;
            if (DoVT100)
                PrintString(VT100_C_NUMBER);
            innumber = true;
            return;
        }
    }

    // check if this is the start of a keyword
    if (isnamechar(*p) && !intext)
    {
        for (i = 0; i < CommandTableSize - 1; i++)
        { // check the command table for a match
            if (EditCompStr((char *)p, (char *)commandtbl[i].name) != 0 ||
                ((EditCompStr((char *)&p[1], (char *)&commandtbl[i].name[1]) != 0) && *p == '.' && *commandtbl[i].name == '_'))
            {
                if (EditCompStr((char *)p, "REM") != 0)
                { // special case, REM is a comment
                    gui_fcolour = GUI_C_COMMENT;
                    if (DoVT100)
                        PrintString(VT100_C_COMMENT);
                    incomment = true;
                }
                else
                {
                    keyword_colour(commandtbl[i].supported, DoVT100);
                    inkeyword = true;
                    if (EditCompStr((char *)p, "GUI") ||
                        EditCompStr((char *)p, "OPTION"))
                    {
                        twokeyword = p;
                        while (isalnum(*twokeyword))
                            twokeyword++;
                        while (*twokeyword == ' ')
                            twokeyword++;
                    }
                    return;
                }
            }
        }
        for (i = 0; i < TokenTableSize - 1; i++)
        { // check the token table for a match
            if (EditCompStr((char *)p, (char *)tokentbl[i].name) != 0)
            {
                keyword_colour(tokentbl[i].supported, DoVT100);
                inkeyword = true;
                return;
            }
        }

        // check for the second keyword in two keyword commands
        if (p == twokeyword)
        {
            for (pp = (unsigned char **)twokeywordtbl; *pp; pp++)
                if (EditCompStr((char *)p, (char *)*pp))
                    break;
            if (*pp)
            {
                gui_fcolour = GUI_C_KEYWORD;
                if (DoVT100)
                    PrintString(VT100_C_KEYWORD);
                inkeyword = true;
                return;
            }
        }
        if (p >= twokeyword)
            twokeyword = NULL;

        // check for a range of common keywords
        for (pp = (unsigned char **)specialkeywords; *pp; pp++)
            if (EditCompStr((char *)p, (char *)*pp))
                break;
        if (*pp)
        {
            gui_fcolour = GUI_C_KEYWORD;
            if (DoVT100)
                PrintString(VT100_C_KEYWORD);
            inkeyword = true;
            return;
        }

        // check for functions that tokenise() rewrites to other tokens
        for (pp = (unsigned char **)hiddenfunctions; *pp; pp++)
            if (EditCompStr((char *)p, (char *)*pp))
                break;
        if (*pp)
        {
            gui_fcolour = GUI_C_KEYWORD;
            if (DoVT100)
                PrintString(VT100_C_KEYWORD);
            inkeyword = true;
            return;
        }

        // check for the MM.xxx functions handled by fun_tilde(); these are rewritten
        // to the ~() token during tokenise() so are not in the token table either
        for (i = 0; i < MMEND; i++)
            if (EditCompStr((char *)p, (char *)overlaid_functions[i]))
            {
                gui_fcolour = GUI_C_KEYWORD;
                if (DoVT100)
                    PrintString(VT100_C_KEYWORD);
                inkeyword = true;
                return;
            }
    }

    // try to keep track of if we are in general text or not
    // this is to avoid recognising keywords or numbers inside variables
    if (isnamechar(*p))
    {
        intext = true;
    }
    else
    {
        intext = false;
        gui_fcolour = GUI_C_NORMAL;
        if (DoVT100)
            PrintString(VT100_C_NORMAL);
    }
}

// Set up SetColour state for the character at 'target' and emit the matching
// VT100 colour escape.  Used by the unmark loops so the terminal colour is
// correct when visible characters are output after an edx-skip.
void restoreColourFromLineStart(unsigned char *target)
{
    int inmulti = 0;
    int ln = 0;
    unsigned char *q;
    for (q = EdBuff; q < target && *q; q++)
        if (*q == '\n')
            ln++;
    SetColour(NULL, false);
    unsigned char *ls = (unsigned char *)findLine(ln, &inmulti);
    if (inmulti == 3)
        SetColour((unsigned char *)1, false);
    else if (inmulti == 4)
        SetColour((unsigned char *)2, false);
    for (q = ls; q < target && *q && *q != '\n'; q++)
        if (!inmulti || inmulti == 3)
            SetColour(q, false);
    if (gui_fcolour == GUI_C_COMMENT)
        PrintString(VT100_C_COMMENT);
    else if (gui_fcolour == GUI_C_QUOTE)
        PrintString(VT100_C_QUOTE);
    else if (gui_fcolour == GUI_C_KEYWORD)
        PrintString(VT100_C_KEYWORD);
    else if (gui_fcolour == GUI_C_KEYWORD_UNSUP)
        PrintString(VT100_C_KEYWORD_UNSUP);
    else if (gui_fcolour == GUI_C_NUMBER)
        PrintString(VT100_C_NUMBER);
    else
        PrintString(VT100_C_NORMAL);
}

// print a line starting at the current column (edx) at the current cursor.
// if the line is beyond the end of the text then just clear to the end of line
// enters with the line number to be printed
void printLine(int ln)
{
    char *p;
    int i;
    int inmulti = false;
    // we always colour code the output to the LCD panel on the MX470 (when used as the console)
    if (Option.DISPLAY_CONSOLE)
    {
        MX470PutC('\r'); // print on the MX470 display
        p = findLine(ln, &inmulti);
        // Init colour state for continuation lines split inside a string or comment
        if (inmulti == 3)
            SetColour((unsigned char *)1, false); // init quote state
        else if (inmulti == 4)
            SetColour((unsigned char *)2, false); // init comment state
        // skip edx characters, advancing colour state without displaying
        for (i = edx; i > 0 && *p && *p != '\n'; i--)
        {
            if (!inmulti || inmulti == 3)
                SetColour((unsigned char *)p, false);
            p++;
        }
        i = VWidth;
        while (i && *p && *p != '\n')
        {
            if (!inmulti || inmulti == 3)
                SetColour((unsigned char *)p, false); // set the colour for the LCD display only
            else
                gui_fcolour = GUI_C_COMMENT;
            MX470PutC(*p++); // print on the MX470 display
            i--;
        }
        MX470Display(CLEAR_TO_EOL); // clear to the end of line on the MX470 display only
    }
    SetColour(NULL, false);

    p = findLine(ln, &inmulti);
    if (Option.ColourCode || edx > 0)
    {
        if (Option.ColourCode)
        {
            // Init colour state for continuation lines split inside a string or comment
            if (inmulti == 3)
                SetColour((unsigned char *)1, true); // init quote state
            else if (inmulti == 4)
                SetColour((unsigned char *)2, true); // init comment state
            // skip edx characters, advancing colour state without emitting codes
            for (i = edx; i > 0 && *p && *p != '\n'; i--)
            {
                if (!inmulti || inmulti == 3)
                    SetColour((unsigned char *)p, false);
                p++;
            }
            // Re-emit the current colour after skipping off-screen chars so the
            // terminal reflects state set by syntax elements that are now off-screen
            // (e.g. a comment ' that has scrolled left of the visible area).
            if (edx > 0)
            {
                if (gui_fcolour == GUI_C_COMMENT)
                    PrintString(VT100_C_COMMENT);
                else if (gui_fcolour == GUI_C_QUOTE)
                    PrintString(VT100_C_QUOTE);
                else if (gui_fcolour == GUI_C_KEYWORD)
                    PrintString(VT100_C_KEYWORD);
                else if (gui_fcolour == GUI_C_KEYWORD_UNSUP)
                    PrintString(VT100_C_KEYWORD_UNSUP);
                else if (gui_fcolour == GUI_C_NUMBER)
                    PrintString(VT100_C_NUMBER);
                else
                    PrintString(VT100_C_NORMAL);
            }
        }
        else
        {
            // no colour coding: just skip edx characters
            for (i = edx; i > 0 && *p && *p != '\n'; i--)
                p++;
        }
        SSputchar('\r', 0); // go to start of line on the VT100 emulator
        i = VWidth;
    }
    else
    {
        // if we are NOT colour coding we can start drawing at the current cursor position
        i = curx;
        while (i-- && *p && *p != '\n')
            p++; // find the editing point in the buffer
        i = VWidth - curx;
    }

    while (i && *p && *p != '\n')
    {
        if (Option.ColourCode)
        {
            if (!inmulti || inmulti == 3)
                SetColour((unsigned char *)p, true); // if colour coding is used set the colour for the VT100 emulator
            else
            {
                gui_fcolour = GUI_C_COMMENT;
                PrintString(VT100_C_COMMENT);
            }
        }
        SSputchar(*p++, 0); // display the chars after the editing point
        i--;
    }

    PrintString("\033[K"); // all done, clear to the end of the line on a vt100 emulator
    if (Option.ColourCode)
        SetColour(NULL, true);
    curx = VWidth - 1;
}

// print a full screen starting with the top left corner specified by edx, edy
// this draws the full screen including blank areas so there is no need to clear the screen first
// it then returns the cursor to its original position
void printScreen(void)
{
    int i;

    SCursor(0, 0);
    for (i = 0; i < VHeight; i++)
    {
        printLine(i + edy);
        PrintString("\r\n");
        MX470PutS("\r\n", gui_fcolour, gui_bcolour);
        curx = 0;
        cury = i + 1;
    }
    while (getConsole() != -1)
        ; // consume any keystrokes accumulated while redrawing the screen
}

// position the cursor on the screen
void SCursor(int x, int y)
{
    char s[12];

    PrintString("\033[");
    IntToStr(s, y + 1, 10);
    PrintString(s);
    PrintString(";");
    IntToStr(s, x + 1, 10);
    PrintString(s);
    PrintString("H");
    MX470Cursor(x * gui_font_width, y * gui_font_height); // position the cursor on the MX470 display only
    curx = x;
    cury = y;
}

// move the text down by one char starting at the current position in the text
// and insert a character
int editInsertChar(unsigned char c, char *multi, int edit_buff_size)
{
    unsigned char *p;

    for (p = EdBuff; *p; p++)
        ; // find the end of the text in memory
    if (p >= EdBuff + edit_buff_size - 10)
    { // and check that we have the space (allow 10 bytes for slack)
        editDisplayMsg((unsigned char *)" OUT OF MEMORY ");
        return false;
    }
    for (; p >= txtp; p--)
        *(p + 1) = *p; // shift everything down
    *multi = 0;
    if (txtp > EdBuff)
    {
        unsigned char prev = *(txtp - 1);
        if ((c == '/' && prev == '*') || (c == '*' && prev == '/'))
            *multi = 1;
    }
    {
        unsigned char next = *(txtp + 1);
        if ((c == '/' && next == '*') || (c == '*' && next == '/'))
            *multi = 1;
    }
    *txtp++ = c; // and insert our char
    return true;
}

// return the character length of the line that txtp sits on
static int current_line_length(void)
{
    unsigned char *p = txtp;
    while (p > EdBuff && *(p - 1) != '\n')
        p--;
    int len = 0;
    while (*p && *p != '\n')
    {
        p++;
        len++;
    }
    return len;
}

// Build the right-aligned status text for the current cursor position into buf.
// Returns its length. Mirrors what PrintStatus draws so PrintFunctKeys can
// reserve exactly the space the status will occupy (rather than always 20).
static int editor_status_string(char *buf)
{
    int tx = edx + curx + 1;
    if (edit_is_bas && current_line_length() > MAXSTRLEN)
        sprintf(buf, "L:%d C:%d %s LONG", edy + cury + 1, tx, insert ? "INS" : "OVR");
    else
        sprintf(buf, "L:%d C:%d %s", edy + cury + 1, tx, insert ? "INS" : "OVR");
    return (int)strlen(buf);
}

// Pick the longest function-key legend that fits in `avail` columns. The
// shortest entry ("EDIT MODE"/"MARK MODE") is always the safe fallback.
static const char *pickFunctKeyString(int typ, int avail)
{
    static const char *edit_keys[] = {
        "ESC:Exit F1:Save F2:Run F3/6:Find/r F4:Mrk F5:Paste F7/8:Rpl/r F9:In F10:Out F12/^A:Beautify",
        "F1:Save F2:Run F3:Find F4:Mark F5:Paste F7:Repl F7/8:Rpl/r F12/^A:Btfy",
        "F1:Save F2:Run F3:Find F4:Mrk F5:Pst F9:In F10:Out ^A:Btfy",
        "F1:Save F2:Run F3:Find F4:Mrk F5:Pst ^A:Btfy",
        "EDIT MODE",
    };
    static const char *mark_keys[] = {
        "MARK MODE  ESC=Exit DEL:Delete F4:Cut F5:Copy F10:Export",
        "MARK MODE  ESC:Exit DEL:Del F4:Cut F5:Cpy F10:Out",
        "MARK MODE",
    };
    const char **list = (typ == EDIT) ? edit_keys : mark_keys;
    int n = (typ == EDIT) ? (int)(sizeof(edit_keys) / sizeof(edit_keys[0]))
                          : (int)(sizeof(mark_keys) / sizeof(mark_keys[0]));
    for (int i = 0; i < n; i++)
    {
        if ((int)strlen(list[i]) <= avail)
            return list[i];
    }
    return list[n - 1];
}

// State shared between PrintFunctKeys and PrintStatus so the status width can
// drive function-key re-layout dynamically as the cursor moves.
static int g_funct_typ = EDIT;
static const char *g_funct_string = NULL;
static int g_status_width = 0;

// print the function keys at the bottom of the screen
void PrintFunctKeys(int typ)
{
    int i, x, y;
    char temp[40];
    int sw = editor_status_string(temp);
    int avail = VWidth - sw - 1; // leave one space before status
    const char *p = pickFunctKeyString(typ, avail);

    g_funct_typ = typ;
    g_funct_string = p;
    g_status_width = sw;

    MX470Display(DRAW_LINE);                         // on the MX470 display draw the line
    MX470PutS((char *)p, GUI_C_STATUS, gui_bcolour); // display the string on the display attached to the MX470
    MX470Display(CLEAR_TO_EOL);                      // clear to the end of line on the MX470 display only

    x = curx;
    y = cury;
    SCursor(0, VHeight);
    if (Option.ColourCode)
        PrintString(VT100_C_LINE);
    PrintString("\033[4m"); // underline on
    for (i = 0; i < VWidth; i++)
        SSputchar(' ', 0);
    PrintString("\033[0m\r\n"); // underline off
    if (Option.ColourCode)
        PrintString(VT100_C_STATUS);
    PrintString((char *)p);
    if (Option.ColourCode)
        PrintString(VT100_C_NORMAL);
    PrintString("\033[K"); // clear to the end of the line on a vt100 emulator
    SCursor(x, y);
}

// print the current status
void PrintStatus(void)
{
    int col;
    char temp[40];

    int cur_width = editor_status_string(temp);
    int avail = VWidth - cur_width - 1;
    const char *want_funct = pickFunctKeyString(g_funct_typ, avail);

    // Redraw the function-key line whenever the legend would change (tier
    // boundary crossed) or the status has shrunk (stale chars from the prior
    // wider status would otherwise linger on the right of the legend).
    if (want_funct != g_funct_string || cur_width < g_status_width)
    {
        PrintFunctKeys(g_funct_typ); // updates g_funct_string / g_status_width
    }
    g_status_width = cur_width;

    col = VWidth - cur_width;
    if (col < 0)
        col = 0;
    MX470Cursor(col * gui_font_width, (VResEdit / gui_font_height) * gui_font_height - gui_font_height);
    MX470PutS(temp, GUI_C_STATUS, gui_bcolour); // display the string on the display attached to the MX470

    SCursor(col, VHeight + 1);
    if (Option.ColourCode)
        PrintString(VT100_C_STATUS);
    PrintString(temp);
    if (Option.ColourCode)
        PrintString(VT100_C_NORMAL);

    PositionCursor(txtp);
}

// display a message in the status line
void editDisplayMsg(unsigned char *msg)
{
    SCursor(0, VHeight + 1);
    if (Option.ColourCode)
        PrintString(VT100_C_ERROR);
    PrintString("\033[7m");
    MX470Cursor(0, (VResEdit / gui_font_height) * gui_font_height - gui_font_height);
    PrintString((char *)msg);
    MX470PutS((char *)msg, BLACK, RED);
    if (Option.ColourCode)
        PrintString(VT100_C_NORMAL);
    PrintString("\033[0m");
    PrintString("\033[K");      // clear to the end of the line on a vt100 emulator
    MX470Display(CLEAR_TO_EOL); // clear to the end of line on the MX470 display only
    PositionCursor(txtp);
    drawstatusline = true;
}

// save the program in the editing buffer into the program memory
void GetInputString(unsigned char *prompt)
{
    int i;
    SCursor(0, VHeight + 1);
    PrintString((char *)prompt);
    MX470Cursor(0, (VResEdit / gui_font_height) * gui_font_height - gui_font_height);
    MX470PutS((char *)prompt, gui_fcolour, gui_bcolour);
    for (i = 0; i < VWidth - strlen((char *)prompt); i++)
    {
        SSputchar(' ', 1);
        MX470PutC(' ');
    }
    SCursor(strlen((char *)prompt), VHeight + 1);
    MX470Cursor(strlen((char *)prompt) * gui_font_width, (VResEdit / gui_font_height) * gui_font_height - gui_font_height);
    int len = 0;
    int maxlen = STRINGSIZE - 1;
    while (1)
    { // get the input
        unsigned char ch = MMgetchar();
        if (ch == '\r')
            break;
        if (ch == 0xb3 || ch == F3 || ch == ESC)
        {
            if (len < maxlen)
                inpbuf[len++] = ch;
            break;
        } // return if it is SHIFT-F3, F3 or ESC
        if (ch == '\b')
        {
            if (len > 0)
            {
                len--;
                PrintString("\b \b");                         // erase on the screen
                MX470PutS("\b \b", gui_fcolour, gui_bcolour); // erase on the MX470 display
            }
            continue;
        }
        if (isprint(ch))
        {
            if (len < maxlen)
            {
                inpbuf[len++] = ch;
                SSputchar(ch, 1); // echo the char
                MX470PutC(ch);    // echo the char on the MX470 display
            }
        }
    }
    inpbuf[len] = 0; // terminate the input string
    PrintFunctKeys(EDIT);
    PositionCursor(txtp);
}

// scroll up the video screen
void Scroll(void)
{
    edy++;
    SCursor(0, VHeight);
    PrintString("\033[J\033[99B\n"); // clear to end of screen, move to the end of the screen and force a scroll of one line
    MX470Cursor(0, VHeight * gui_font_height);
    MX470Scroll(gui_font_height);
    SCursor(0, VHeight);
    curx = 0;
    cury = VHeight - 1;
    PrintFunctKeys(EDIT);
    printLine(VHeight - 1 + edy);
    PositionCursor(txtp);
    while (getConsole() != -1)
        ; // consume any keystrokes accumulated while redrawing the screen
}

// scroll down the video screen
void ScrollDown(void)
{
    SCursor(0, VHeight);   // go to the end of the editing area
    PrintString("\033[J"); // clear to end of screen
    edy--;
    SCursor(0, 0);
    PrintString("\033M"); // scroll window down one line
    MX470Scroll(-gui_font_height);
    printLine(edy);
    PrintFunctKeys(EDIT);
    PositionCursor(txtp);

    while (getConsole() != -1)
        routinechecks();
    ; // consume any keystrokes accumulated while redrawing the screen
}

// the longest line in the buffer, and which line it was - the .bas save
// path refuses to write a line MMBasic could not read back
int find_longest_line_length(const char *text, int *linein)
{
    int current_length = 0;
    int max_length = 0;
    const char *ptr = text;
    int line = 0;
    while (*ptr)
    {
        if (*ptr == '\n')
        {
            line++;
            if (ptr > text && *(ptr - 1) == '_' && *(ptr - 2) == ' ' && Option.continuation)
            {
                // Line continuation, do not reset length
            }
            else
            {
                // If this line exceeds the max, update
                if (current_length > max_length)
                {
                    max_length = current_length;
                    *linein = line;
                }
                current_length = 0; // Reset for a new line
            }
        }
        else
        {
            // Increase length for this segment of the line
            current_length++;
        }

        ptr++;
    }

    // Final check in case the last line was the longest
    if (current_length > max_length)
    {
        max_length = current_length;
    }

    return max_length;
}

/* mark mode - not ported yet.  It is 800 self-contained lines and the
 * only thing that fills the clipboard, so it says so rather than doing
 * nothing silently. */
void MarkMode(unsigned char *cb, unsigned char *buf)
{
    (void)cb;
    (void)buf;
    editDisplayMsg((unsigned char *)" MARK MODE NOT PORTED YET ");
}

void FullScreenEditor(int xx, int yy, char *fname, int edit_buff_size,
                      bool_t cmdfile)
{
    int c = -1, i;
    unsigned char buf[MAXCLIP + 2], clipboard[MAXCLIP + 2];
    unsigned char *p, *tp;
    char currdel = 0, nextdel = 0, lastdel = 0;
    char multi = false;
    bool_t foundit = false;

    edit_is_bas = (fname != NULL && strstr(fname, ".bas") != NULL);
    (void)cmdfile;
    printScreen(); // draw the screen
    SCursor(xx, yy);
    drawstatusline = true;
    unsigned char lastkey = 0;
    int y, statuscount;
    clipboard[0] = 0;
    buf[0] = 0;
    insert = true;
    TextChanged = false;
    while (1)
    {
        statuscount = 0;
        ShowCursor(true);
        /* Once, not once per poll.  MMBasic's ShowCursor drove a hardware
         * cursor and cost nothing to repeat; here it is an escape
         * sequence down the same serial line the editor draws on, and at
         * ten polls a second an idle editor would never stop talking. */
        do
        {
            c = MMInkey();
            /* MMBasic polls flat out and refreshes the status after
             * 5000 empty passes.  Here a pass costs 100ms (the read
             * timeout that also separates a bare ESC from a sequence),
             * so the same idea needs a much smaller count. */
            if (statuscount++ == 50)
                PrintStatus();
        } while (c == -1);
        ShowCursor(false);

        if (drawstatusline)
            PrintFunctKeys(EDIT);
        drawstatusline = false;
        if (c == TAB)
        {
            strcpy((char *)buf, "        ");
            buf[Option.Tab - ((edx + curx) % Option.Tab)] = 0;
        }
        else
        {
            buf[0] = c;
            buf[1] = 0;
        }
        if (!(buf[0] == SHIFT_F5 || buf[0] == SHIFT_F4 || buf[0] == F7 || buf[0] == F8))
            foundit = false;
        do
        {
            switch (buf[0])
            {

            case '\r':
            case '\n': // first count the spaces at the beginning of the line
                if (txtp != EdBuff && (*txtp == '\n' || *txtp == 0))
                { // we only do this if we are at the end of the line
                    for (tp = txtp - 1, i = 0; *tp != '\n' && tp >= EdBuff; tp--)
                        if (*tp != ' ')
                            i = 0; // not a space
                        else
                            i++; // potential space at the start
                    if (tp == EdBuff && *tp == ' ')
                        i++; // correct for a counting error at the start of the buffer
                    if (buf[1] != 0)
                        i = 0; // do not insert spaces if buffer too small or has something in it
                    else
                        buf[i + 1] = 0; // make sure that the end of the buffer is zeroed
                    while (i)
                        buf[i--] = ' '; // now, place our spaces in the typeahead buffer
                }
                if (!editInsertChar('\n', &multi, edit_buff_size))
                    break; // insert the newline
                TextChanged = true;
                nbrlines++;
                if (!(cury < VHeight - 1)) // if we are NOT at the bottom
                    edy++;                 // otherwise scroll
                edx = 0;                   // reset horizontal scroll for the new line
                printScreen();             // redraw everything
                PositionCursor(txtp);
                break;

            case CTRLKEY('E'):
            case UP:
                if (cury == 0 && edy == 0)
                    break;
                if (edx != 0)
                {
                    edx = 0;
                    SCursor(0, cury);
                    printLine(edy + cury);
                }
                if (*txtp == '\n')
                    txtp--; // step back over the terminator if we are right at the end of the line
                while (txtp != EdBuff && *txtp != '\n')
                    txtp--; // move to the beginning of the line
                if (txtp != EdBuff)
                {
                    txtp--; // step over the terminator to the end of the previous line
                    while (txtp != EdBuff && *txtp != '\n')
                        txtp--; // move to the beginning of that line
                    if (*txtp == '\n')
                        txtp++; // and position at the start
                }
                for (i = 0; i < edx + tempx && *txtp != 0 && *txtp != '\n'; i++, txtp++)
                    ; // move the cursor to the column

                if (cury > 2 || edy == 0)
                { // if we are more that two lines from the top
                    if (cury > 0)
                        SCursor(i, cury - 1); // just move the cursor up
                }
                else if (edy > 0)
                { // if we are two lines or less from the top
                    curx = i;
                    ScrollDown();
                }
                PositionCursor(txtp);
                break;

            case CTRLKEY('X'):
            case DOWN:
                if (edx != 0)
                {
                    edx = 0;
                    SCursor(0, cury);
                    printLine(edy + cury);
                }
                p = txtp;
                while (*p != 0 && *p != '\n')
                    p++; // move to the end of this line
                if (*p == 0)
                    break; // skip if it is at the end of the file
                p++;       // step over the line terminator to the start of the next line
                for (i = 0; i < edx + tempx && *p != 0 && *p != '\n'; i++, p++)
                    ; // move the cursor to the column
                txtp = p;

                if (cury < VHeight - 3 || edy + VHeight == nbrlines)
                {
                    if (cury < VHeight - 1)
                        SCursor(i, cury + 1);
                }
                else if (edy + VHeight < nbrlines)
                {
                    curx = i;
                    Scroll();
                }
                PositionCursor(txtp);
                break;

            case CTRLKEY('S'):
            case LEFT:
                if (txtp == EdBuff)
                    break;
                if (*(txtp - 1) == '\n')
                { // if at the beginning of the line wrap around
                    buf[1] = UP;
                    buf[2] = END;
                    buf[3] = 0;
                    buf[4] = 0;
                }
                else
                {
                    txtp--;
                    if (edx > 0 && curx <= SCROLLCHARS)
                    {
                        edx--;
                        printLine(edy + cury);
                    }
                    PositionCursor(txtp);
                }
                break;

            case CTRLKEY('D'):
            case RIGHT:
                if (*txtp == '\n')
                { // if at the end of the line wrap around
                    buf[1] = HOME;
                    buf[2] = DOWN;
                    buf[3] = 0;
                    break;
                }
                if (*txtp != 0)
                {
                    txtp++;
                    if (curx >= VWidth - 1 - SCROLLCHARS)
                    {
                        edx++;
                        printLine(edy + cury);
                    }
                }
                PositionCursor(txtp);
                break;

            // backspace
            case BKSP:
                if (txtp == EdBuff)
                    break;
                if (*(txtp - 1) == '\n')
                { // if at the beginning of the line wrap around
                    buf[1] = UP;
                    buf[2] = END;
                    buf[3] = DEL;
                    buf[4] = 0;
                    break;
                }
                // find how many spaces are between the cursor and the start of the line
                for (p = txtp - 1; *p == ' ' && p != EdBuff; p--)
                    ;
                if ((p == EdBuff || *p == '\n') && txtp - p > 1)
                {
                    i = txtp - p - 1;
                    // we have have the number of continuous spaces between the cursor and the start of the line
                    // now figure out the number of backspaces to the nearest tab stop

                    i = (i % Option.Tab);
                    if (i == 0)
                        i = Option.Tab;
                    // load the corresponding number of deletes in the type ahead buffer
                    buf[i + 1] = 0;
                    while (i--)
                    {
                        buf[i + 1] = DEL;
                        txtp--;
                    }
                    // and let the delete case take care of deleting the characters
                    PositionCursor(txtp);
                    break;
                }
                // this is just a normal backspace (not a tabbed backspace)
                txtp--;
                if (edx > 0 && curx <= SCROLLCHARS)
                    edx--;
                PositionCursor(txtp);
                // fall through to delete the char

            case CTRLKEY(']'):
            case DEL:
                if (*txtp == 0)
                    break;
                p = txtp;
                c = *p;
                currdel = *p;
                if (p != EdBuff + edit_buff_size - 1)
                    nextdel = p[1];
                else
                    nextdel = 0;
                if (p != EdBuff)
                {
                    lastdel = *(--p);
                    p++;
                }
                else
                    lastdel = 0;
                while (*p)
                {
                    p[0] = p[1];
                    p++;
                }
                if (c == '\n')
                {
                    // Measure the cursor's column (= length of original line n).
                    // txtp still points to the now-deleted '\n' position, so count
                    // chars back to the start of the line.
                    int abs_col = 0;
                    {
                        unsigned char *q = txtp;
                        while (q > EdBuff && *(q - 1) != '\n')
                        {
                            abs_col++;
                            q--;
                        }
                    }
                    int saved_cury = cury;
                    int cur_ln = edy + saved_cury;
                    // Reset horizontal scroll before full redraw so every line
                    // is drawn left-justified, not offset by edx from the END key.
                    edx = 0;
                    printScreen();
                    nbrlines--;
                    // If the join point is off-screen to the right, scroll only
                    // the current (joined) line to bring the cursor into view.
                    if (abs_col > VWidth - 1)
                    {
                        edx = abs_col - (VWidth - 1 - SCROLLCHARS);
                        SCursor(0, saved_cury);
                        printLine(cur_ln);
                    }
                }
                else
                    printLine(edy + cury);
                TextChanged = true;
                PositionCursor(txtp);
                if (currdel == '/' && nextdel == '*' && Option.ColourCode)
                    printScreen();
                if (currdel == '*' && nextdel == '/' && Option.ColourCode)
                    printScreen();
                if (currdel == '/' && lastdel == '*' && Option.ColourCode)
                    printScreen();
                if (currdel == '*' && lastdel == '/' && Option.ColourCode)
                    printScreen();
                break;

            case CTRLKEY('N'):
            case INSERT:
                insert = !insert;
                break;

            case CTRLKEY('U'):
            case HOME:
                if (txtp == EdBuff)
                    break;
                if (lastkey == HOME || lastkey == CTRLKEY('U'))
                {
                    edx = edy = curx = cury = 0;
                    txtp = EdBuff;
                    PrintString("\033[2J\033[H"); // vt100 clear screen and home cursor
                    MX470Display(DISPLAY_CLS);    // clear screen on the MX470 display only
                    printScreen();
                    PrintFunctKeys(EDIT);
                    PositionCursor(txtp);
                    break;
                }
                if (*txtp == '\n')
                    txtp--; // step back over the terminator if we are right at the end of the line
                while (txtp != EdBuff && *txtp != '\n')
                    txtp--; // move to the beginning of the line
                if (*txtp == '\n')
                    txtp++; // skip if no more lines above this one
                if (edx != 0)
                {
                    edx = 0;
                    SCursor(0, cury);
                    printLine(edy + cury);
                }
                PositionCursor(txtp);
                break;

            case CTRLKEY('K'):
            case END:
                if (*txtp == 0)
                    break; // already at the end
                if (lastkey == END || lastkey == CTRLKEY('K'))
                { // jump to the end of the file
                    i = 0;
                    p = txtp = EdBuff;
                    while (*txtp != 0)
                    {
                        if (*txtp == '\n')
                        {
                            p = txtp + 1;
                            i++;
                        }
                        txtp++;
                    }

                    if (i >= VHeight)
                    {
                        edy = i - VHeight + 1;
                        printScreen();
                        cury = VHeight - 1;
                    }
                    else
                    {
                        cury = i;
                    }
                    txtp = p;
                    curx = 0;
                }

                while (*txtp != 0 && *txtp != '\n')
                    txtp++;
                {
                    int abs_col = 0;
                    unsigned char *q = txtp;
                    while (q > EdBuff && *(q - 1) != '\n')
                    {
                        abs_col++;
                        q--;
                    }
                    if (abs_col > VWidth - 1)
                    {
                        edx = abs_col - (VWidth - 1 - SCROLLCHARS);
                        SCursor(0, cury);
                        printLine(edy + cury);
                    }
                    else if (edx != 0)
                    {
                        edx = 0;
                        SCursor(0, cury);
                        printLine(edy + cury);
                    }
                }
                PositionCursor(txtp);
                break;

            case CTRLKEY('P'):
            case PUP:
                if (edy == 0)
                {                  // if we are already showing the top of the text
                    buf[1] = HOME; // force the editing point to the start of the text
                    buf[2] = HOME;
                    buf[3] = 0;
                    break;
                }
                else if (edy >= VHeight - 1)
                { // if we can scroll a full screenfull
                    i = VHeight + 1;
                    edy -= VHeight;
                }
                else
                { // if it is less than a full screenfull
                    i = edy + 1;
                    edy = 0;
                }
                while (i--)
                {
                    if (*txtp == '\n')
                        txtp--; // step back over the terminator if we are right at the end of the line
                    while (txtp != EdBuff && *txtp != '\n')
                        txtp--; // move to the beginning of the line
                    if (txtp == EdBuff)
                        break; // skip if no more lines above this one
                }
                if (txtp != EdBuff)
                    txtp++; // and position at the start of the line
                for (i = 0; i < edx + curx && *txtp != 0 && *txtp != '\n'; i++, txtp++)
                    ; // move the cursor to the column
                printScreen();
                PositionCursor(txtp);
                break;

            case CTRLKEY('L'):
            case PDOWN:
                if (nbrlines <= edy + VHeight + 1)
                {                 // if we are already showing the end of the text
                    buf[1] = END; // force the editing point to the end of the text
                    buf[2] = END;
                    buf[3] = 0;
                    break; // cursor to the top line
                }
                else if (nbrlines - edy - VHeight >= VHeight)
                { // if we can scroll a full screenfull
                    edy += VHeight;
                    i = VHeight;
                }
                else
                { // if it is less than a full screenfull
                    i = nbrlines - VHeight - edy;
                    edy = nbrlines - VHeight;
                }
                if (*txtp == '\n')
                    i--; // compensate if we are right at the end of a line
                while (i--)
                {
                    if (*txtp == '\n')
                        txtp++; // step over the terminator if we are right at the start of the line
                    while (*txtp != 0 && *txtp != '\n')
                        txtp++; // move to the end of the line
                    if (*txtp == 0)
                        break; // skip if no more lines after this one
                }
                if (txtp != EdBuff)
                    txtp++; // and position at the start of the line
                for (i = 0; i < edx + curx && *txtp != 0 && *txtp != '\n'; i++, txtp++)
                    ; // move the cursor to the column
                // y = cury;
                printScreen();
                PositionCursor(txtp);
                break;

            // Abort without saving
            case ESC:
                /* MMBasic looked here for a TeraTerm mouse report, by
                 * peeking for '[' 'M' after a 50ms gap.  Removed: our
                 * inkey() reassembles escape sequences itself, so a
                 * sequence never reaches this switch as a bare ESC, and
                 * peeking would eat the first byte of the user's next
                 * keystroke instead. */
                if (TextChanged)
                {
                    GetInputString((unsigned char *)"Exit and discard all changes (Y/N): ");
                    if (mytoupper(*inpbuf) != 'Y')
                        break;
                }
                // fall through to the normal exit

            case CTRLKEY('Q'): // Save and exit
            case F1:           // Save and exit
            case CTRLKEY('W'): // Save, exit and run
            case F2:           // Save, exit and run
            {
                /* MMBasic saved through its own file layer, and for the
                 * in-memory program into flash; here it is a plain file
                 * plus a .bak, and CRLF becomes LF.  The line-length
                 * refusal is kept for .bas only: MMBasic cannot read
                 * back a line longer than 255, so writing one would make
                 * a file the interpreter chokes on. */
                int line = 0;
                int longest = find_longest_line_length((char *)EdBuff, &line);
                int quit = buf[0];

                if (edit_is_bas && longest > MAXSTRLEN)
                {
                    char buff[32];
                    sprintf(buff, " LINE %d TOO LONG ", line);
                    editDisplayMsg((unsigned char *)buff);
                    break;
                }
                if (quit != ESC && TextChanged && fname)
                {
                    if (file_backup(fname) < 0 || file_save(fname) < 0)
                    {
                        editDisplayMsg((unsigned char *)" CANNOT SAVE ");
                        break;
                    }
                }
                PrintString("\033[?7h");           // restore autowrap
                PrintString(VT100_C_NORMAL);
                PrintString("\033[0m");
                PrintString("\033[2J\033[H");      // clear screen, home cursor
                gui_fcolour = GUI_C_NORMAL;
                /* F2 is "save, exit and run" on a PicoMite.  There is no
                 * interpreter to hand the program back to, so it is
                 * reported and the caller decides. */
                editor_exit_key = quit;
                return;
            }

            // Search
            case CTRLKEY('R'):
            case F3:
                GetInputString((unsigned char *)"Find (Use SHIFT-F3 to repeat): ");
                if (*inpbuf == 0 || *inpbuf == ESC)
                    break;
                if (!(*inpbuf == 0xb3 || *inpbuf == F3))
                    strcpy((char *)tknbuf, (char *)inpbuf);
                // fall through

            case CTRLKEY('G'):
            case SHIFT_F3:
            case F6:
                p = txtp;
                if (*p == 0)
                    p = EdBuff - 1;
                i = strlen((char *)tknbuf);
                while (1)
                {
                    p++;
                    if (p == txtp)
                        break;
                    if (*p == 0)
                        p = EdBuff;
                    if (p == txtp)
                        break;
                    if (mem_equal(p, tknbuf, i))
                        break;
                }
                if (p == txtp)
                {
                    editDisplayMsg((unsigned char *)" NOT FOUND ");
                    break;
                }
                for (y = 0, txtp = EdBuff; txtp != p; txtp++)
                { // find the line and column of the string
                    if (*txtp == '\n')
                    {
                        y++; // y is the line
                    }
                }
                edy = y - VHeight / 2; // edy is the line displayed at the top
                if (edy < 0)
                    edy = 0; // compensate if we are near the start
                printScreen();
                PositionCursor(txtp);
                foundit = true;
                // SCursor(x, y);
                break;

            // Mark
            case CTRLKEY('T'):
            case F4:
                MarkMode(clipboard, &buf[1]);
                {
                    // While selecting along a long line, mark-mode navigation drags
                    // edx (the horizontal scroll) to the right but only redraws the
                    // current line.  If we now redraw the whole screen with that
                    // stale edx every background line is left scrolled and short
                    // lines become unreachable.  Mirror the line-join fix: redraw
                    // left-justified, then scroll only the cursor's own line if its
                    // column is off-screen to the right.
                    int abs_col = 0;
                    unsigned char *q = txtp;
                    while (q > EdBuff && *(q - 1) != '\n')
                    {
                        abs_col++;
                        q--;
                    }
                    edx = 0;
                    printScreen();
                    PrintFunctKeys(EDIT);
                    if (abs_col > VWidth - 1)
                    {
                        edx = abs_col - (VWidth - 1 - SCROLLCHARS);
                        SCursor(0, cury);
                        printLine(edy + cury);
                    }
                    curx = abs_col - edx;
                }
                PositionCursor(txtp);
                break;
            case SHIFT_F4:
            case F7:
            case CTRLKEY('F'):
                GetInputString((unsigned char *)"Replace: ");
                if (*inpbuf == 0 || *inpbuf == ESC)
                    break;
                if (!(*inpbuf == 0xb3 || *inpbuf == F3))
                    strcpy((char *)clipboard, (char *)inpbuf);
            case CTRLKEY('I'):
            case SHIFT_F5:
            case F8:
                if (!foundit)
                {
                    editDisplayMsg((unsigned char *)" NOTHING TO REPLACE ");
                    break;
                }
                if (*clipboard == 0)
                {
                    editDisplayMsg((unsigned char *)" CLIPBOARD IS EMPTY ");
                    break;
                }
                for (i = 0; i < strlen((char *)tknbuf); i++)
                {
                    buf[i + 1] = DEL;
                }
                buf[i + 1] = F5;
                break;
            case CTRLKEY('Y'):
            case CTRLKEY('V'):
            case F5:
                if (*clipboard == 0)
                {
                    editDisplayMsg((unsigned char *)" CLIPBOARD IS EMPTY ");
                    break;
                }
                for (i = 0; clipboard[i]; i++)
                    buf[i + 1] = clipboard[i];
                buf[i + 1] = 0;
                break;

            // F9 - Import a file at the current position
            case CTRLKEY('O'):
            case F9:
            {
                int fd, n, inserted = 0;
                unsigned char *saved_txtp;
                unsigned char ch;
                char msg[40];

                GetInputString((unsigned char *)"Import file: ");
                if (*inpbuf == 0 || *inpbuf == ESC)
                    break;
                fd = open((char *)inpbuf, O_RDONLY);
                if (fd < 0)
                {
                    editDisplayMsg((unsigned char *)" CANNOT OPEN FILE ");
                    break;
                }
                saved_txtp = txtp;
                while ((n = read(fd, &ch, 1)) == 1)
                {
                    if (ch == '\r')
                        continue;               // a DOS file arriving
                    if (ch == '\n')
                        nbrlines++;
                    if (!editInsertChar(ch, &multi, edit_buff_size))
                        break;                  // editInsertChar said why
                    inserted++;
                }
                close(fd);
                txtp = saved_txtp;              // before the inserted text
                TextChanged = true;
                edx = 0;
                printScreen();
                PositionCursor(txtp);
                sprintf(msg, " IMPORTED %d CHARS ", inserted);
                editDisplayMsg((unsigned char *)msg);
            }
            break;

            // F10 - Export the clipboard to a file
            case CTRLKEY('B'):
            case F10:
            {
                int fd, len;

                if (clipboard[0] == 0)
                {
                    editDisplayMsg((unsigned char *)" CLIPBOARD IS EMPTY ");
                    break;
                }
                GetInputString((unsigned char *)"Export to file: ");
                if (*inpbuf == 0 || *inpbuf == ESC)
                    break;
                fd = open((char *)inpbuf, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0)
                {
                    editDisplayMsg((unsigned char *)" CANNOT CREATE FILE ");
                    break;
                }
                len = strlen((char *)clipboard);
                if (write(fd, clipboard, len) != len)
                    editDisplayMsg((unsigned char *)" WRITE FAILED ");
                else
                {
                    char msg[40];
                    sprintf(msg, " EXPORTED %d CHARS ", len);
                    editDisplayMsg((unsigned char *)msg);
                }
                close(fd);
            }
            break;

            case F11:
                break;

            /* F12 / Ctrl-A is MMBasic's beautifier - 500 lines of block
             * re-indentation, not ported yet. */
            case CTRLKEY('A'):
            case F12:
                editDisplayMsg((unsigned char *)" BEAUTIFY NOT PORTED YET ");
                break;

            // a normal character
            default:
                c = buf[0];
                if (c < ' ' || c > '~')
                    break; // make sure that this is valid
                TextChanged = true;
                if (insert || *txtp == '\n' || *txtp == 0)
                {
                    if (!editInsertChar(c, &multi, edit_buff_size))
                        break; // insert it
                }
                else
                    *txtp++ = c; // or just overtype
                if (curx >= VWidth - 1 - SCROLLCHARS)
                    edx++;
                printLine(edy + cury); // redraw the whole line so that colour coding will occur
                PositionCursor(txtp);
                // SCursor(x, cury);
                tempx = cury; // used to track the preferred cursor position
                if (multi && Option.ColourCode)
                    printScreen();
                PrintStatus();
                break;
            }
            lastkey = buf[0];
            if (buf[0] != UP && buf[0] != DOWN && buf[0] != CTRLKEY('E') && buf[0] != CTRLKEY('X'))
                tempx = curx;
            buf[MAXCLIP + 1] = 0;
            for (i = 0; i < MAXCLIP + 1; i++)
                buf[i] = buf[i + 1]; // suffle down the buffer to get the next char
        } while (*buf);
    }
}

/*******************************************************************************************************************
  UTILITY FUNCTIONS USED BY THE FULL SCREEN EDITOR
*******************************************************************************************************************/

void PositionCursor(unsigned char *curp)
{
    int ln, col;
    unsigned char *p;

    for (p = EdBuff, ln = col = 0; p < curp; p++)
    {
        if (*p == '\n')
        {
            ln++;
            col = 0;
        }
        else
            col++;
    }
    if (ln < edy || ln >= edy + VHeight)
        return;
    SCursor(col - edx, ln - edy);
}

// mark mode
// implement the mark mode (when the user presses F4)

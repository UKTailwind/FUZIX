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
/* Set while mark mode paints the highlight.  MMBasic's tile renderer
 * read it to suppress its own colouring; nothing reads it here, but the
 * ported code sets it and it costs a byte to keep the port honest. */
static bool_t markmode;
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
    static int intext = false;
    static int incomment = false;
    static int inkeyword = false;
    static unsigned char *twokeyword = NULL;
    static int inquote = false;
    static int innumber = false;

    if (!Option.ColourCode)
        return;

    /* The OPTION second words, the type names and OPEN modes, the
     * functions tokenise() rewrites, and the MM.xxx set: all four lists
     * are generated in keywords.c now, each name carrying whether mmbc
     * can translate it.  They used to be written out here and painted
     * as keywords whatever they were, which told the reader that
     * MM.WATCHDOG, MM.INFO$ and OPTION BAUDRATE would compile. */

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
            for (i = 0; twokeyword_tbl[i].name; i++)
                if (EditCompStr((char *)p, (char *)twokeyword_tbl[i].name))
                {
                    keyword_colour(twokeyword_tbl[i].supported, DoVT100);
                    inkeyword = true;
                    return;
                }
        }
        if (p >= twokeyword)
            twokeyword = NULL;

        // check for a range of common keywords
        for (i = 0; special_keywords[i].name; i++)
            if (EditCompStr((char *)p, (char *)special_keywords[i].name))
            {
                keyword_colour(special_keywords[i].supported, DoVT100);
                inkeyword = true;
                return;
            }

        // check for functions that tokenise() rewrites to other tokens
        for (i = 0; hidden_functions[i].name; i++)
            if (EditCompStr((char *)p, (char *)hidden_functions[i].name))
            {
                keyword_colour(hidden_functions[i].supported, DoVT100);
                inkeyword = true;
                return;
            }

        // check for the MM.xxx functions handled by fun_tilde(); these are rewritten
        // to the ~() token during tokenise() so are not in the token table either
        for (i = 0; i < MMEND; i++)
            if (EditCompStr((char *)p, (char *)overlaid_functions[i].name))
            {
                keyword_colour(overlaid_functions[i].supported, DoVT100);
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
    /* (int)strlen: MMBasic writes VWidth - strlen(prompt), which is int
       minus size_t and so unsigned - a prompt longer than the screen is
       wide underflows to a huge count and the editor sits there printing
       spaces.  It cannot happen at 80 columns, where every prompt fits,
       but a narrow terminal would hang the editor rather than truncate
       the padding. */
    for (i = 0; i < VWidth - (int)strlen((char *)prompt); i++)
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
        /* Both spellings of Enter.  MMBasic only needed CR because its
           console gave it one; here the two consoles disagree - a serial
           terminal sends CR, and the USB keyboard's map sends LF (10,
           keyboard_maps.h) - so this prompt accepted Enter from TeraTerm
           and ignored it from the keyboard on the machine's own screen.
           F3 was where it showed: the prompt appeared, the search string
           typed and echoed, and Enter did nothing while ESC still
           cancelled.  The editor's main loop has always taken both. */
        if (ch == '\r' || ch == '\n')
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

/* Mark mode - MMBasic's, Editor.c 7402-8208.
 *
 * F4 enters it from the main loop; the cursor keys move the far end of
 * the selection while txtp holds the anchor, and F4/F5 cut or copy the
 * span into the clipboard the caller passed in.  Everything here is
 * buffer arithmetic and VT100 output, so it arrives almost unchanged:
 * the mouse blocks are gone, the ESC handler no longer peeks for a
 * TeraTerm mouse report (inkey() reassembles sequences itself, so it
 * would only ever eat the next keystroke), and F10 export writes with
 * open/write instead of MMBasic's file layer - no CR added, this being
 * a Unix filesystem.
 *
 * This is also the first thing to exercise restoreColourFromLineStart:
 * un-highlighting has to put the syntax colour back for a character in
 * the middle of a line, which means replaying the colour state machine
 * from the start of that line.
 */
void MarkMode(unsigned char *cb, unsigned char *buf)
{
    unsigned char *p, *mark, *oldmark;
    int c = -1, x, y, i, oldx, oldy, txtpx, txtpy, errmsg = false;
    int edx_save = edx, edy_save = edy;
    PrintFunctKeys(MARK);
    oldmark = mark = txtp;
    txtpx = oldx = curx;
    txtpy = oldy = cury;
    while (1)
    {
        c = MMInkey();
        if (c != -1 && errmsg)
        {
            PrintFunctKeys(MARK);
            errmsg = false;
        }
        switch (c)
        {
        case ESC:
            /* MMBasic peeked for a TeraTerm mouse report here.  Removed
             * for the same reason as in FullScreenEditor: inkey()
             * reassembles escape sequences itself, so a sequence never
             * arrives as a bare ESC, and peeking would swallow the
             * first byte of the user's next keystroke instead. */
            curx = txtpx;
            cury = txtpy; // just an escape key
            edx = edx_save;
            edy = edy_save;
            SCursor(curx, cury);
            return;

        case CTRLKEY('E'):
        case UP:
            if (cury <= 0 && edy == 0)
                continue; // at very top of file
            p = mark;
            if (*p == '\n')
                p--; // step back over the terminator if we are right at the end of the line
            while (p != EdBuff && *p != '\n')
                p--; // move to the beginning of the line
            if (p != EdBuff)
            {
                p--; // step over the terminator to the end of the previous line
                for (i = 0; p != EdBuff && *p != '\n'; p--, i++)
                    ; // move to the beginning of that line
                if (*p == '\n')
                    p++; // and position at the start
            }
            mark = p;
            for (i = 0; i < edx + curx && *mark != 0 && *mark != '\n'; i++, mark++)
                ; // move the cursor to the preferred absolute column
            {
                int need_redraw = 0;
                if (i >= edx + VWidth - SCROLLCHARS)
                {
                    edx = i - (VWidth - 1 - SCROLLCHARS);
                    need_redraw = 1;
                }
                else if (edx > 0 && i < edx + SCROLLCHARS)
                {
                    edx = (i > SCROLLCHARS) ? i - SCROLLCHARS : 0;
                    need_redraw = 1;
                }
                curx = i - edx;
                if (cury > 0)
                    cury--;
                else if (edy > 0)
                {
                    edy--;
                    need_redraw = 1;
                }
                if (need_redraw)
                {
                    printScreen();
                    PrintFunctKeys(MARK);
                }
            }
            break;

        case CTRLKEY('X'):
        case DOWN:
            for (p = mark; *p != 0 && *p != '\n'; p++)
                ; // move to the end of this line
            if (*p == 0)
                continue; // skip if it is at the end of the file
            mark = p + 1; // step over the line terminator to the start of the next line
            for (i = 0; i < edx + curx && *mark != 0 && *mark != '\n'; i++, mark++)
                ; // move the cursor to the preferred absolute column
            {
                int need_redraw = 0;
                if (i >= edx + VWidth - SCROLLCHARS)
                {
                    edx = i - (VWidth - 1 - SCROLLCHARS);
                    need_redraw = 1;
                }
                else if (edx > 0 && i < edx + SCROLLCHARS)
                {
                    edx = (i > SCROLLCHARS) ? i - SCROLLCHARS : 0;
                    need_redraw = 1;
                }
                curx = i - edx;
                if (cury < VHeight - 1)
                    cury++;
                else if (edy + VHeight < nbrlines)
                {
                    edy++;
                    need_redraw = 1;
                }
                if (need_redraw)
                {
                    printScreen();
                    PrintFunctKeys(MARK);
                }
            }
            break;

        case CTRLKEY('S'):
        case LEFT:
            if (curx == 0 && edx == 0)
                continue;
            mark--;
            if (edx > 0 && curx <= SCROLLCHARS)
            {
                edx--;
                printLine(edy + cury);
            }
            else
                curx--;
            break;

        case CTRLKEY('D'):
        case RIGHT:
            if (*mark == 0 || *mark == '\n')
                continue;
            mark++;
            if (curx >= VWidth - 1 - SCROLLCHARS)
            {
                edx++;
                printLine(edy + cury);
            }
            else
                curx++;
            break;

        case CTRLKEY('U'):
        case HOME:
            if (mark == EdBuff)
                break;
            if (*mark == '\n')
                mark--; // step back over the terminator if we are right at the end of the line
            while (mark != EdBuff && *mark != '\n')
                mark--; // move to the beginning of the line
            if (*mark == '\n')
                mark++; // skip if no more lines above this one
            if (edx != 0)
            {
                edx = 0;
                printScreen();
                PrintFunctKeys(MARK);
            }
            curx = 0;
            break;

        case CTRLKEY('K'):
        case END:
            if (*mark == 0)
                break;
            for (p = mark; *p != 0 && *p != '\n'; p++)
                ; // move to the end of this line
            mark = p;
            {
                int abs_col = 0;
                unsigned char *q = mark;
                while (q > EdBuff && *(q - 1) != '\n')
                {
                    abs_col++;
                    q--;
                }
                if (abs_col > VWidth - 1)
                {
                    edx = abs_col - (VWidth - 1 - SCROLLCHARS);
                    printScreen();
                    PrintFunctKeys(MARK);
                }
                else if (edx != 0)
                {
                    edx = 0;
                    printScreen();
                    PrintFunctKeys(MARK);
                }
                curx = abs_col - edx;
            }
            break;

        case CTRLKEY('P'):
        case PUP:
            if (edy == 0)
            {
                // Already at top, move mark to beginning of file
                mark = EdBuff;
                cury = 0;
                curx = 0;
            }
            else
            {
                // Scroll up by VHeight lines
                int scrollamt = (edy >= VHeight) ? VHeight : edy;
                edy -= scrollamt;
                // Move mark up by scrollamt lines
                for (i = 0; i < scrollamt && mark != EdBuff; i++)
                {
                    if (*mark == '\n')
                        mark--;
                    while (mark != EdBuff && *mark != '\n')
                        mark--;
                    if (*mark == '\n' && mark != EdBuff)
                        mark--;
                    while (mark != EdBuff && *mark != '\n')
                        mark--;
                    if (*mark == '\n')
                        mark++;
                }
                // Position at start of line
                while (mark != EdBuff && *(mark - 1) != '\n')
                    mark--;
                for (i = 0; i < edx + curx && *mark != 0 && *mark != '\n'; i++, mark++)
                    ;
                if (i >= edx + VWidth - SCROLLCHARS)
                    edx = i - (VWidth - 1 - SCROLLCHARS);
                else if (edx > 0 && i < edx + SCROLLCHARS)
                    edx = (i > SCROLLCHARS) ? i - SCROLLCHARS : 0;
                curx = i - edx;
                printScreen();
                PrintFunctKeys(MARK);
            }
            break;

        case CTRLKEY('L'):
        case PDOWN:
            if (edy + VHeight >= nbrlines)
            {
                // Already showing end, move mark to end of file
                while (*mark)
                    mark++;
                cury = VHeight - 1;
            }
            else
            {
                // Scroll down by VHeight lines
                int scrollamt = VHeight;
                if (edy + VHeight + scrollamt > nbrlines)
                    scrollamt = nbrlines - edy - VHeight;
                edy += scrollamt;
                // Move mark down by scrollamt lines
                for (i = 0; i < scrollamt && *mark; i++)
                {
                    while (*mark && *mark != '\n')
                        mark++;
                    if (*mark == '\n')
                        mark++;
                }
                // Position at column
                for (i = 0; i < edx + curx && *mark != 0 && *mark != '\n'; i++, mark++)
                    ;
                if (i >= edx + VWidth - SCROLLCHARS)
                    edx = i - (VWidth - 1 - SCROLLCHARS);
                else if (edx > 0 && i < edx + SCROLLCHARS)
                    edx = (i > SCROLLCHARS) ? i - SCROLLCHARS : 0;
                curx = i - edx;
                printScreen();
                PrintFunctKeys(MARK);
            }
            break;

        case CTRLKEY('Y'):
        case CTRLKEY('T'):
        case F5:
        case F4:
        case CTRLKEY('B'):
        case F10:
            if (c != F10 && (txtp - mark > MAXCLIP || mark - txtp > MAXCLIP))
            {
                editDisplayMsg((unsigned char *)" MARKED TEXT EXCEEDS BUFFER SIZE");
                errmsg = true;
                SCursor(curx, cury);
                continue;
            }
            if (c != F10)
            {
                if (mark <= txtp)
                {
                    p = mark;
                    while (p < txtp)
                        *cb++ = *p++;
                }
                else
                {
                    p = txtp;
                    while (p <= mark - 1)
                        *cb++ = *p++;
                }
                *cb = 0;
            }
            if (c == F5 || c == CTRLKEY('Y') || c == F10)
            {
                // For F10, also export to file before returning
                if (c == F10)
                {
                    GetInputString((unsigned char *)"Export to file: ");
                    if (*inpbuf != 0 && *inpbuf != ESC)
                    {
                        int fd = open((char *)inpbuf,
                                      O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (fd < 0)
                            editDisplayMsg((unsigned char *)" CANNOT CREATE FILE ");
                        else
                        {
                            /* The marked span, low to high.  cb has been
                             * advanced by the copy above, so recompute it
                             * rather than trusting that pointer - and no
                             * CR is added, because this is a Unix file. */
                            unsigned char *cp = (mark <= txtp) ? mark : txtp;
                            unsigned char *cpend = (mark <= txtp) ? txtp : mark;
                            int len = (int)(cpend - cp);
                            char msg[40];

                            if (len > 0 && write(fd, cp, len) != len)
                                editDisplayMsg((unsigned char *)" WRITE FAILED ");
                            else
                            {
                                sprintf(msg, " EXPORTED %d CHARS ", len);
                                editDisplayMsg((unsigned char *)msg);
                            }
                            close(fd);
                        }
                    }
                }
                // Calculate line number of txtp and adjust edy if needed
                int ln;
                unsigned char *pp;
                for (pp = EdBuff, ln = 0; pp < txtp; pp++)
                    if (*pp == '\n')
                        ln++;
                // If txtp is off-screen, adjust edy to center it
                if (ln < edy || ln >= edy + VHeight)
                {
                    edy = ln - VHeight / 2;
                    if (edy < 0)
                        edy = 0;
                    if (edy + VHeight > nbrlines)
                        edy = (nbrlines > VHeight) ? nbrlines - VHeight : 0;
                }
                cury = ln - edy;
                PositionCursor(txtp);
                return;
            }
            // fall through

        case CTRLKEY(']'):
        case DEL:
            if (mark < txtp)
            {
                p = txtp;
                txtp = mark;
                mark = p; // swap txtp and mark
            }
            for (p = txtp; p < mark; p++)
                if (*p == '\n')
                    nbrlines--;
            for (p = txtp; *mark;)
                *p++ = *mark++;
            *p++ = 0;
            *p++ = 0;
            TextChanged = true;
            // Calculate line number of txtp and adjust edy if needed
            {
                int ln;
                unsigned char *pp;
                for (pp = EdBuff, ln = 0; pp < txtp; pp++)
                    if (*pp == '\n')
                        ln++;
                // If txtp is off-screen, adjust edy to center it
                if (ln < edy || ln >= edy + VHeight)
                {
                    edy = ln - VHeight / 2;
                    if (edy < 0)
                        edy = 0;
                    if (edy + VHeight > nbrlines)
                        edy = (nbrlines > VHeight) ? nbrlines - VHeight : 0;
                }
                cury = ln - edy;
            }
            PositionCursor(txtp);
            return;
        case 9999:
            break;
        default:
            continue;
        }

        x = curx;
        y = cury;
        markmode = true;

        // Calculate visible line range
        unsigned char *visStart, *visEnd;
        int ln;
        for (visStart = EdBuff, ln = 0; ln < edy && *visStart; visStart++)
            if (*visStart == '\n')
                ln++;
        for (visEnd = visStart, ln = 0; ln < VHeight && *visEnd; visEnd++)
            if (*visEnd == '\n')
                ln++;

        // Determine the marked range (low to high)
        unsigned char *markLow = (mark < txtp) ? mark : txtp;
        unsigned char *markHigh = (mark < txtp) ? txtp : mark;

        // first unmark the area not marked as a result of the keystroke (only visible portion)
        // col tracks the absolute text column so we clip output to [edx, edx+VWidth)
        if (oldmark < mark)
        {
            p = oldmark;
            int col = 0;
            {
                unsigned char *q = p;
                while (q > EdBuff && *(q - 1) != '\n')
                {
                    col++;
                    q--;
                }
            }
            if (p >= visStart && p < visEnd)
            {
                while (p < mark && *p != '\n' && col < edx)
                {
                    col++;
                    p++;
                }
                if (p < mark && p >= visStart && p < visEnd && *p != '\n')
                {
                    PositionCursor(p);
                    if (Option.ColourCode)
                        restoreColourFromLineStart(p);
                }
            }
            while (p < mark)
            {
                if (p >= visStart && p < visEnd)
                {
                    if (*p == '\n')
                    {
                        col = 0;
                        p++;
                        while (p < mark && p < visEnd && *p != '\n' && col < edx)
                        {
                            col++;
                            p++;
                        }
                        if (p < mark && p >= visStart && p < visEnd && *p != '\n')
                        {
                            PositionCursor(p);
                            if (Option.ColourCode)
                                restoreColourFromLineStart(p);
                        }
                        continue;
                    }
                    if (col >= edx && col < edx + VWidth)
                    {
                        if (Option.ColourCode)
                            SetColour(p, true);
                        MX470PutC(*p);
                        SSputchar(*p, 0);
                    }
                }
                if (*p == '\n')
                    col = 0;
                else
                    col++;
                p++;
            }
        }
        else if (oldmark > mark)
        {
            p = mark;
            int col = 0;
            {
                unsigned char *q = p;
                while (q > EdBuff && *(q - 1) != '\n')
                {
                    col++;
                    q--;
                }
            }
            if (p >= visStart && p < visEnd)
            {
                while (oldmark > p && *p != '\n' && col < edx)
                {
                    col++;
                    p++;
                }
                if (oldmark > p && p >= visStart && p < visEnd && *p != '\n')
                {
                    PositionCursor(p);
                    if (Option.ColourCode)
                        restoreColourFromLineStart(p);
                }
            }
            while (oldmark > p)
            {
                if (p >= visStart && p < visEnd)
                {
                    if (*p == '\n')
                    {
                        col = 0;
                        p++;
                        while (oldmark > p && p < visEnd && *p != '\n' && col < edx)
                        {
                            col++;
                            p++;
                        }
                        if (oldmark > p && p >= visStart && p < visEnd && *p != '\n')
                        {
                            PositionCursor(p);
                            if (Option.ColourCode)
                                restoreColourFromLineStart(p);
                        }
                        continue;
                    }
                    if (col >= edx && col < edx + VWidth)
                    {
                        if (Option.ColourCode)
                            SetColour(p, true);
                        MX470PutC(*p);
                        SSputchar(*p, 0);
                    }
                }
                if (*p == '\n')
                    col = 0;
                else
                    col++;
                p++;
            }
        }
        oldmark = mark;
        oldx = x;
        oldy = y;

        // now draw the marked area (only visible portion)
        if (markLow < markHigh)
        {
            // Find where to start drawing (intersection of marked range and visible range)
            unsigned char *drawStart = (markLow > visStart) ? markLow : visStart;
            unsigned char *drawEnd = (markHigh < visEnd) ? markHigh : visEnd;

            if (drawStart < drawEnd)
            {
                PrintString(Option.ColourCode ? "\033[44m" : "\033[0m\033[7m");
                MX470Display(REVERSE_VIDEO);
                p = drawStart;
                int col = 0;
                {
                    unsigned char *q = p;
                    while (q > EdBuff && *(q - 1) != '\n')
                    {
                        col++;
                        q--;
                    }
                }
                while (p < drawEnd && *p != '\n' && col < edx)
                {
                    col++;
                    p++;
                }
                if (p < drawEnd && *p != '\n')
                    PositionCursor(p);
                while (p < drawEnd)
                {
                    if (*p == '\n')
                    {
                        col = 0;
                        p++;
                        while (p < drawEnd && *p != '\n' && col < edx)
                        {
                            col++;
                            p++;
                        }
                        if (p < drawEnd && *p != '\n')
                            PositionCursor(p);
                        continue;
                    }
                    if (col >= edx && col < edx + VWidth)
                    {
                        MX470PutC(*p);
                        SSputchar(*p, 0);
                    }
                    col++;
                    p++;
                }
                MX470Display(REVERSE_VIDEO);
            }
        }
        markmode = false;
        PrintString("\033[0m"); // normal video

        oldx = x;
        oldy = y;
        oldmark = mark;
        PositionCursor(mark);
    }
}
/* The beautifier - MMBasic's, Editor.c 5536-6105.
 *
 * Re-indents block structures two spaces per level: IF/ELSE/ELSEIF/END
 * IF, FOR/NEXT, DO/LOOP, SELECT CASE/CASE/END SELECT, SUB, FUNCTION and
 * TYPE.  A single-line IF (statements after THEN) keeps the prevailing
 * indent without opening a block.
 *
 * It arrives verbatim - it touches nothing but EdBuff and
 * editDisplayMsg, so there was nothing to adapt.  STRUCTENABLED is
 * defined below because MMBasic 6.x has TYPE and its own editor indents
 * those blocks; mmbc not translating TYPE yet is beside the point, the
 * editor is for MMBasic source in general.
 */
#define STRUCTENABLED 1

static int beautify_token_match(const char *up, int idx, const char *kw)
{
    int klen = (int)strlen(kw);
    for (int i = 0; i < klen; i++)
    {
        if (up[idx + i] != kw[i])
            return 0;
    }
    char nxt = up[idx + klen];
    return (nxt == 0 || nxt == ' ' || nxt == '\t' || nxt == ':');
}

/* Returns non-zero if the buffer contains any blank line that is NOT
 * immediately preceded by an END SUB / END FUNCTION line.  Used by the
 * F12 handler to decide whether to prompt the user about preserving
 * blank lines. */
static int beautify_has_stray_blanks(void)
{
    unsigned char *p = EdBuff;
    int prev_was_end_sub_or_func = 0;
    while (*p)
    {
        unsigned char *line_start = p;
        unsigned char *eol = p;
        while (*eol && *eol != '\n')
            eol++;

        unsigned char *t = line_start;
        while (t < eol && (*t == ' ' || *t == '\t'))
            t++;
        int blank = (t >= eol);

        if (blank)
        {
            if (!prev_was_end_sub_or_func)
                return 1;
            prev_was_end_sub_or_func = 0; /* don't allow a 2nd freebie */
        }
        else
        {
            char up[16] = {0};
            int ulen = 0;
            for (unsigned char *q = t; q < eol && ulen < (int)sizeof(up) - 1; q++)
            {
                char ch = (char)*q;
                if (ch >= 'a' && ch <= 'z')
                    ch = (char)(ch - 32);
                up[ulen++] = ch;
            }
            prev_was_end_sub_or_func =
                (beautify_token_match(up, 0, "END") &&
                 (beautify_token_match(up, 4, "SUB") ||
                  beautify_token_match(up, 4, "FUNCTION")));
        }
        p = eol + (*eol == '\n' ? 1 : 0);
    }
    return 0;
}

static void editBeautify(int edit_buff_size, int keep_blanks)
{
    int line_number_base = 0; /* if >0, programs uses line numbers and this
                               * is the column at which statements start
                               * (= longest line-number width + 1).        */
    /* ---- Pass 1: optionally remove blank/whitespace-only lines, and at the
     *               same time work out whether the program is line-numbered.
     *               It is considered numbered iff the first non-blank,
     *               non-comment line begins with a run of digits followed by
     *               whitespace, ':' or end-of-line.  When that is the case
     *               we record the width of the longest such leading number
     *               so that all subsequent lines can be aligned past it.
     *               When keep_blanks is non-zero we leave blank lines in
     *               place (they are still emitted to the output buffer) but
     *               otherwise behave identically.                          */
    {
        unsigned char *src = EdBuff;
        unsigned char *dst = EdBuff;
        int saw_first = 0;
        int numbered_program = 0;
        int max_num_len = 0;
        int prev_emitted_blank = 0;
        while (*src)
        {
            unsigned char *line_start = src;
            unsigned char *eol = src;
            while (*eol && *eol != '\n')
                eol++;
            unsigned char *t = line_start;
            while (t < eol && (*t == ' ' || *t == '\t'))
                t++;
            int blank = (t >= eol);
            int line_len = (int)(eol - line_start) + (*eol == '\n' ? 1 : 0);
            if (blank)
            {
                if (keep_blanks && !prev_emitted_blank)
                {
                    /* Emit a single normalised blank line ('\n').  Because
                     * dst <= line_start (we only ever shrink), writing one
                     * byte is always safe. */
                    *dst++ = '\n';
                    prev_emitted_blank = 1;
                }
                /* otherwise: drop the blank line entirely */
            }
            else
            {
                /* Is this line a comment-only line? */
                int is_cmt = 0;
                if (*t == '\'')
                    is_cmt = 1;
                else if ((eol - t) >= 3 &&
                         (t[0] == 'R' || t[0] == 'r') &&
                         (t[1] == 'E' || t[1] == 'e') &&
                         (t[2] == 'M' || t[2] == 'm') &&
                         ((eol - t) == 3 || t[3] == ' ' || t[3] == '\t'))
                    is_cmt = 1;

                /* Measure leading line-number, if any. */
                int num_len = 0;
                {
                    const unsigned char *r = t;
                    while (r < eol && *r >= '0' && *r <= '9')
                        r++;
                    if (r > t && (r >= eol || *r == ' ' || *r == '\t' ||
                                  *r == ':'))
                        num_len = (int)(r - t);
                }
                if (!saw_first && !is_cmt)
                {
                    saw_first = 1;
                    if (num_len > 0)
                        numbered_program = 1;
                }
                if (numbered_program && num_len > max_num_len)
                    max_num_len = num_len;

                if (dst != line_start)
                    memmove(dst, line_start, line_len);
                dst += line_len;
                prev_emitted_blank = 0;
            }
            src = line_start + line_len;
        }
        *dst = 0;
        if (numbered_program)
            line_number_base = max_num_len + 1;
    }

    /* ---- Pass 2: insert one blank line after each END SUB / END FUNCTION
     *               (except at end of buffer).  Inserting after the end of
     *               a routine - rather than before its SUB/FUNCTION header -
     *               keeps any leading comment block attached to the routine
     *               that it documents.                                     */
    {
        unsigned char *p = EdBuff;
        while (*p)
        {
            unsigned char *line_start = p;
            unsigned char *eol = p;
            while (*eol && *eol != '\n')
                eol++;

            unsigned char *s = line_start;
            while (s < eol && (*s == ' ' || *s == '\t'))
                s++;

            char up[16] = {0};
            int ulen = 0;
            for (unsigned char *q = s; q < eol && ulen < (int)sizeof(up) - 1; q++)
            {
                char ch = (char)*q;
                if (ch >= 'a' && ch <= 'z')
                    ch = (char)(ch - 32);
                up[ulen++] = ch;
            }

            int is_end_sub_or_func = (beautify_token_match(up, 0, "END") &&
                                      (beautify_token_match(up, 4, "SUB") ||
                                       beautify_token_match(up, 4, "FUNCTION")));

            /* Move to start of the next line. */
            unsigned char *next_line = eol + (*eol == '\n' ? 1 : 0);

            if (is_end_sub_or_func && *next_line)
            {
                /* If the next line is already blank, no need to insert one. */
                int next_is_blank = 0;
                {
                    unsigned char *u = next_line;
                    while (*u == ' ' || *u == '\t')
                        u++;
                    if (*u == '\n' || *u == 0)
                        next_is_blank = 1;
                }
                if (!next_is_blank)
                {
                    /* Insert a single '\n' at next_line.  Need 1 byte of room. */
                    unsigned char *bufend = next_line;
                    while (*bufend)
                        bufend++;
                    if ((bufend - EdBuff) + 1 >= edit_buff_size - 10)
                    {
                        editDisplayMsg((unsigned char *)" BEAUTIFY: BUFFER FULL ");
                        return;
                    }
                    int tail_len = (int)(bufend - next_line) + 1; /* incl. NUL */
                    memmove(next_line + 1, next_line, tail_len);
                    *next_line = '\n';
                    next_line++;
                }
            }

            p = next_line;
        }
    }

    /* ---- Pass 3: re-indent each line based on block structure ---- */
    int level = 0;
    unsigned char *p = EdBuff;

    while (*p)
    {
        unsigned char *line_start = p;

        /* Find current end-of-line and the existing leading whitespace */
        unsigned char *eol = p;
        while (*eol && *eol != '\n')
            eol++;

        unsigned char *s = line_start;
        while (s < eol && (*s == ' ' || *s == '\t'))
            s++;

        /* Build a small uppercase token buffer for keyword sniffing */
        char up[40] = {0};
        int ulen = 0;
        for (unsigned char *q = s; q < eol && ulen < (int)sizeof(up) - 1; q++)
        {
            char ch = (char)*q;
            if (ch >= 'a' && ch <= 'z')
                ch = (char)(ch - 32);
            up[ulen++] = ch;
        }

        int dedent_self = 0;
        int indent_after = 0;
        int is_blank = (s >= eol);
        int is_comment = 0;
        int is_label = 0;
        int is_line_number = 0;
        int line_number_len = 0;
        if (!is_blank)
        {
            if (*s == '\'')
                is_comment = 1;
            else if (ulen >= 3 && up[0] == 'R' && up[1] == 'E' && up[2] == 'M' &&
                     (ulen == 3 || up[3] == ' ' || up[3] == '\t'))
                is_comment = 1;
            else if (*s >= '0' && *s <= '9')
            {
                /* Detect a leading line number: a run of digits followed by
                 * whitespace, end-of-line or ':'.  Treated like a label so
                 * that classic line-numbered BASIC is not pushed off to the
                 * right by surrounding block structure. */
                const unsigned char *r = s;
                while (r < eol && *r >= '0' && *r <= '9')
                    r++;
                if (r > s && (r >= eol || *r == ' ' || *r == '\t' || *r == ':'))
                {
                    is_line_number = 1;
                    line_number_len = (int)(r - s);
                }
            }
            else
            {
                /* Detect a leading label: identifier ([A-Za-z_][A-Za-z0-9_]*)
                 * immediately followed by ':'.  Such lines are kept flush
                 * left so that targets of GOTO/GOSUB stay visible even when
                 * they appear inside an indented block. */
                unsigned char c0 = *s;
                if ((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z') ||
                    c0 == '_')
                {
                    const unsigned char *r = s + 1;
                    while (r < eol)
                    {
                        unsigned char ch = *r;
                        if ((ch >= 'A' && ch <= 'Z') ||
                            (ch >= 'a' && ch <= 'z') ||
                            (ch >= '0' && ch <= '9') || ch == '_')
                            r++;
                        else
                            break;
                    }
                    if (r < eol && *r == ':')
                        is_label = 1;
                }
            }
        }

        if (!is_blank && !is_comment)
        {
            /* Walk the line statement-by-statement, splitting at ':' outside
             * string literals and stopping at the start of any trailing
             * comment.  For each statement classify whether it opens
             * (FOR, DO, SUB, FUNCTION, TYPE, SELECT CASE, multi-line IF),
             * closes (NEXT, LOOP, END IF / ENDIF, END SELECT / SUB / FUNCTION /
             * TYPE) or is a middle clause (ELSE, ELSEIF, CASE).  Accumulate
             * the net indent change for the line so that single-line forms
             * such as  FOR i=1 TO 9:a(i)=i:NEXT  cancel out and do not
             * indent following lines.                                       */
            const unsigned char *q = s;
            /* Skip a leading line number so that the keyword classifier
             * sees the actual statement (e.g. DO/LOOP/SUB/END SUB) that
             * follows the digits. */
            if (is_line_number)
                q = s + line_number_len;
            int first_stmt = 1;
            int in_str = 0;
            while (q < eol)
            {
                while (q < eol && (*q == ' ' || *q == '\t'))
                    q++;
                const unsigned char *stmt_start = q;
                const unsigned char *stmt_end = NULL;

                while (q < eol)
                {
                    char ch = (char)*q;
                    if (ch == '"')
                        in_str = !in_str;
                    else if (ch == '\'' && !in_str)
                    {
                        stmt_end = q;
                        q = eol; /* trailing comment ends scanning */
                        break;
                    }
                    else if (ch == ':' && !in_str)
                        break;
                    q++;
                }
                if (stmt_end == NULL)
                    stmt_end = q;
                if (q < eol && *q == ':')
                    q++;

                if (stmt_end <= stmt_start)
                {
                    first_stmt = 0;
                    continue;
                }

                char kw[16] = {0};
                int klen = 0;
                for (const unsigned char *r = stmt_start;
                     r < stmt_end && klen < (int)sizeof(kw) - 1; r++)
                {
                    char ch = (char)*r;
                    if (ch >= 'a' && ch <= 'z')
                        ch = (char)(ch - 32);
                    kw[klen++] = ch;
                }

                int open_kw = 0, close_kw = 0, middle_kw = 0;

                if (beautify_token_match(kw, 0, "ENDIF") ||
                    beautify_token_match(kw, 0, "NEXT") ||
                    beautify_token_match(kw, 0, "LOOP") ||
                    (beautify_token_match(kw, 0, "END") &&
                     (beautify_token_match(kw, 4, "IF") ||
                      beautify_token_match(kw, 4, "SELECT") ||
                      beautify_token_match(kw, 4, "FUNCTION") ||
                      beautify_token_match(kw, 4, "SUB")
#ifdef STRUCTENABLED
                      || beautify_token_match(kw, 4, "TYPE")
#endif
                          )))
                    close_kw = 1;
                else if (beautify_token_match(kw, 0, "ELSE") ||
                         beautify_token_match(kw, 0, "ELSEIF") ||
                         beautify_token_match(kw, 0, "CASE"))
                    middle_kw = 1;

                if (beautify_token_match(kw, 0, "FOR") ||
                    beautify_token_match(kw, 0, "DO") ||
                    beautify_token_match(kw, 0, "FUNCTION") ||
                    beautify_token_match(kw, 0, "SUB")
#ifdef STRUCTENABLED
                    || beautify_token_match(kw, 0, "TYPE")
#endif
                    || (beautify_token_match(kw, 0, "SELECT") &&
                        beautify_token_match(kw, 7, "CASE")))
                    open_kw = 1;
                else if (beautify_token_match(kw, 0, "IF"))
                {
                    int found_after = -1;
                    for (const unsigned char *r = stmt_start + 2;
                         r + 4 < stmt_end; r++)
                    {
                        if ((r[0] == ' ' || r[0] == '\t') &&
                            (r[1] == 'T' || r[1] == 't') &&
                            (r[2] == 'H' || r[2] == 'h') &&
                            (r[3] == 'E' || r[3] == 'e') &&
                            (r[4] == 'N' || r[4] == 'n'))
                        {
                            const unsigned char *after = r + 5;
                            if (after == stmt_end || *after == ' ' || *after == '\t')
                            {
                                found_after = (int)(after - stmt_start);
                                break;
                            }
                        }
                    }
                    if (found_after >= 0)
                    {
                        const unsigned char *a = stmt_start + found_after;
                        while (a < stmt_end && (*a == ' ' || *a == '\t'))
                            a++;
                        if (a >= stmt_end)
                            open_kw = 1;
                    }
                }

                if (first_stmt)
                {
                    if (close_kw)
                        dedent_self = 1;
                    else if (middle_kw)
                    {
                        dedent_self = 1;
                        indent_after += 1;
                    }
                    if (open_kw)
                        indent_after += 1;
                }
                else
                {
                    if (close_kw)
                        indent_after -= 1;
                    else if (open_kw)
                        indent_after += 1;
                    /* middle clauses inside a colon-joined line are
                     * structurally unusual; treat as no-op.                */
                }

                first_stmt = 0;
            }
        }

        int line_indent = level - dedent_self;
        if (line_indent < 0)
            line_indent = 0;
        int want_spaces;
        if (is_blank)
            want_spaces = 0;
        else if (is_label)
            want_spaces = 0; /* labels stay flush left */
        else if (is_line_number)
            want_spaces = 0; /* digits go to col 0; gap fixed up below */
        else
            want_spaces = line_number_base + line_indent * 2;
        int have_spaces = (int)(s - line_start);

        /* Adjust leading whitespace in place by shifting the tail of the buffer */
        if (want_spaces != have_spaces)
        {
            int diff = want_spaces - have_spaces; /* >0 grow, <0 shrink */

            /* Find end of buffer (NUL terminator) */
            unsigned char *bufend = s;
            while (*bufend)
                bufend++;
            int tail_len = (int)(bufend - s); /* bytes from s through, not incl. NUL */

            if (diff > 0)
            {
                /* Need to grow: ensure there's room (10-byte slack like editInsertChar) */
                if ((bufend - EdBuff) + diff >= edit_buff_size - 10)
                {
                    editDisplayMsg((unsigned char *)" BEAUTIFY: BUFFER FULL ");
                    return;
                }
                /* Shift tail (including NUL) right by diff */
                memmove(s + diff, s, tail_len + 1);
            }
            else
            {
                /* Shrink: shift tail (including NUL) left by -diff */
                memmove(s + diff, s, tail_len + 1);
            }

            /* Fill new leading spaces */
            for (int i = 0; i < want_spaces; i++)
                line_start[i] = ' ';

            /* Recompute eol after shift */
            eol = line_start + want_spaces;
            while (*eol && *eol != '\n')
                eol++;
        }
        else
        {
            /* Same width - just rewrite spaces (in case some were tabs) */
            for (int i = 0; i < want_spaces; i++)
                line_start[i] = ' ';
        }

        /* For numbered lines, normalise the gap between the line number and
         * the following statement so the statement aligns at
         * line_number_base + line_indent*2 (with a minimum of one space). */
        if (is_line_number)
        {
            unsigned char *num_end = line_start + line_number_len;
            unsigned char *gap_end = num_end;
            while (*gap_end == ' ' || *gap_end == '\t')
                gap_end++;
            if (*gap_end != 0 && *gap_end != '\n')
            {
                int have_gap = (int)(gap_end - num_end);
                int target_col = line_number_base + line_indent * 2;
                int want_gap = target_col - line_number_len;
                if (want_gap < 1)
                    want_gap = 1;
                int diff = want_gap - have_gap;
                if (diff != 0)
                {
                    unsigned char *bufend = gap_end;
                    while (*bufend)
                        bufend++;
                    int tail_len = (int)(bufend - gap_end);
                    if (diff > 0 &&
                        (bufend - EdBuff) + diff >= edit_buff_size - 10)
                    {
                        editDisplayMsg((unsigned char *)" BEAUTIFY: BUFFER FULL ");
                        return;
                    }
                    memmove(gap_end + diff, gap_end, tail_len + 1);
                }
                for (int i = 0; i < want_gap; i++)
                    num_end[i] = ' ';
                eol = num_end + want_gap;
                while (*eol && *eol != '\n')
                    eol++;
            }
        }

        /* Advance to the character after the newline (or end) */
        p = eol;
        if (*p == '\n')
            p++;

        if (dedent_self)
            level--;
        if (indent_after)
            level++;
        if (level < 0)
            level = 0;
    }
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
        /* BEFORE the wait, not after it.  This used to sit below the
         * key-polling loop, so the function-key line was only drawn
         * once a key had been pressed: entering the editor left the
         * bottom line blank until the reader either typed something or
         * waited out the 50 empty polls - five seconds - that make
         * PrintStatus redraw it.  That was the reported symptom, and
         * the same off-by-one keystroke applied to every later redraw
         * printScreen asked for. */
        if (drawstatusline)
            PrintFunctKeys(EDIT);
        drawstatusline = false;
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

            // F12 / Ctrl-A - Beautify (re-indent block structures)
            case CTRLKEY('A'):
            case F12:
            {
                int keep_blanks = 0;
                if (beautify_has_stray_blanks())
                {
                    /* Prompt the user.  Y (default) keeps blank lines,
                     * N removes them. */
                    editDisplayMsg((unsigned char *)" KEEP BLANK LINES? (Y/N) ");
                    while (1)
                    {
                        int kc = MMgetchar();
                        if (kc == 'y' || kc == 'Y' || kc == '\r' || kc == '\n')
                        {
                            keep_blanks = 1;
                            break;
                        }
                        if (kc == 'n' || kc == 'N')
                        {
                            keep_blanks = 0;
                            break;
                        }
                        if (kc == ESC)
                        {
                            /* Cancel beautify entirely. */
                            PrintStatus();
                            goto beautify_done;
                        }
                    }
                }
                editBeautify(edit_buff_size, keep_blanks);
                TextChanged = true;
                /* After buffer rewrite, restore cursor to start of buffer */
                txtp = EdBuff;
                edx = edy = curx = cury = 0;
                nbrlines = buf_count_lines();
                printScreen();
                PositionCursor(txtp);
                PrintStatus();
                editDisplayMsg((unsigned char *)" BEAUTIFIED ");
            beautify_done:;
            }
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
        /* Line and column, refreshed after every key.  MMBasic only did
         * this on an edit and relied on the idle counter above to catch
         * cursor movement - which was free there, because its poll loop
         * spun.  Here a poll costs 100ms, so the same counter left the
         * status seconds out of date after arrowing around.  Redrawing
         * it costs a couple of dozen bytes down the wire. */
        PrintStatus();
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

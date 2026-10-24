/* mmbc_tab.c - the static word tables, copied verbatim from mmb2c.py.
 * Order is preserved (it is documentation); lookups are linear - the
 * tables are small and the pass count is low. */

#include "mmbc.h"

/* Words that can never be a variable name. */
static const char *keywords[] = {
    "THEN", "ELSE", "GOTO", "GOSUB", "TO", "STEP", "FOR", "WHILE", "UNTIL",
    "LOAD", "MOD", "NOT", "AND", "OR", "XOR", "AS", "INV", "IS", "CASE",
    "SELECT", "IF", "ENDIF", "END", "SUB", "FUNCTION", "EXIT", "DIM",
    "LOCAL", "STATIC", "CONST", "PRINT", "LET", "DO", "LOOP", "WEND",
    "NEXT", "OPTION", "REM", "BYVAL", "BYREF", "INTEGER", "FLOAT",
    "STRING", "CALL", "RETURN", "BASE", "EXPLICIT", "DEFAULT", "NONE",
    "OPEN", "CLOSE", "INPUT", "OUTPUT", "APPEND", "RANDOM", "SEEK",
    "KILL", "RENAME", "MKDIR", "RMDIR", "CHDIR", "COPY", "FILES", "ALL",
    "DATA", "READ", "RESTORE", "SORT", "CONTINUE", "INC", "CAT", "ERASE",
    "CLEAR", "PAUSE", "ERROR", "ARRAY", "SAVE", "PRESERVE", "LONGSTRING",
    NULL
};

/* Built-in functions we can translate: name, min args, max args. */
static const struct builtin builtins[] = {
    { "ABS", 1, 1 }, { "INT", 1, 1 }, { "FIX", 1, 1 }, { "CINT", 1, 1 },
    { "SGN", 1, 1 }, { "SQR", 1, 1 }, { "SIN", 1, 1 }, { "COS", 1, 1 },
    { "TAN", 1, 1 }, { "ATN", 1, 1 }, { "ASIN", 1, 1 }, { "ACOS", 1, 1 },
    { "ATAN2", 2, 2 }, { "DEG", 1, 1 }, { "RAD", 1, 1 },
    { "LOG", 1, 1 }, { "EXP", 1, 1 },
    { "RND", 0, 1 }, { "PI", 0, 0 }, { "MAX", 2, 64 }, { "MIN", 2, 64 },
    { "LEN", 1, 1 }, { "ASC", 1, 1 }, { "VAL", 1, 1 }, { "INSTR", 2, 3 },
    { "TAB", 1, 1 }, { "TIMER", 0, 0 }, { "BIT", 2, 2 }, { "BYTE", 2, 2 },
    { "CHR$", 1, 1 }, { "LEFT$", 2, 2 }, { "RIGHT$", 2, 2 }, { "MID$", 2, 3 },
    { "STR$", 1, 4 }, { "HEX$", 1, 2 }, { "OCT$", 1, 2 }, { "BIN$", 1, 2 },
    { "UCASE$", 1, 1 }, { "LCASE$", 1, 1 }, { "SPACE$", 1, 1 },
    { "STRING$", 2, 2 }, { "LTRIM$", 1, 1 }, { "RTRIM$", 1, 1 },
    { "FORMAT$", 1, 2 },
    { "DATE$", 0, 0 }, { "TIME$", 0, 0 }, { "CWD$", 0, 0 },
    { "INKEY$", 0, 0 },
    { "EOF", 1, 1 }, { "LOC", 1, 1 }, { "LOF", 1, 1 }, { "INPUT$", 2, 2 },
    { "CHOICE", 3, 3 }, { "BOUND", 1, 2 }, { "TRIM$", 1, 3 },
    { "FIELD$", 2, 4 },
    { "DATETIME$", 1, 1 }, { "DAY$", 1, 1 }, { "EPOCH", 1, 1 },
    { "BIN2STR$", 2, 3 }, { "STR2BIN", 2, 3 }, { "RGB", 1, 3 },
    { "MATH", 1, 1 },
    { "PIXEL", 2, 2 },
    { "MAP", 1, 1 },
    { "PIN", 1, 1 },
    { "PORT", 2, 16 },
    { "FLAG", 1, 1 },
    { "SPI", 1, 1 },
    { "POS", 0, 0 },
    { "TEMPR", 1, 2 },
    { "PULSIN", 2, 4 }, { "DISTANCE", 1, 2 },
    { "MM.ONEWIRE", 0, 0 }, { "MM.I2C", 0, 0 },
    { "MM.HRES", 0, 0 }, { "MM.VRES", 0, 0 },
    { "MM.SPISPEED", 0, 0 },
    { "MM.ERRNO", 0, 0 }, { "MM.ERRMSG$", 0, 0 },
    { "MM.MESSAGE$", 0, 0 }, { "MM.ADDRESS$", 0, 0 },
    { "MM.VER", 0, 0 }, { "MM.DEVICE$", 0, 0 },
    { "MM.CMDLINE$", 0, 0 },
    { "MM.FONTHEIGHT", 0, 0 }, { "MM.FONTWIDTH", 0, 0 },
    { "MM.HPOS", 0, 0 }, { "MM.VPOS", 0, 0 },
    { "MM.INFO", 1, 1 }, { "MM.INFO$", 1, 1 },
    { "KEYDOWN", 1, 1 },
    { "PEEK", 1, 1 }, { "SPRITE", 1, 3 }, { "TILEMAP", 1, 6 },
    { "DRAW3D", 1, 1 },
    { "DIR$", 0, 2 },
    { "LLEN", 1, 1 }, { "LGETSTR$", 3, 3 }, { "LGETBYTE", 2, 2 },
    { "LINSTR", 2, 3 }, { "LCOMPARE", 2, 2 }, { "LINPUT", 3, 3 },
    { "JSON$", 2, 2 },
    { NULL, 0, 0 }
};

/* built-ins whose arguments cannot be parsed as plain expressions */
static const char *rawarg[] = {
    "CHOICE", "BOUND", "TRIM$", "DATETIME$", "DAY$", "EPOCH",
    "BIN2STR$", "STR2BIN", "RGB", "MATH",
    "MM.INFO", "MM.INFO$", "PEEK", "SPRITE", "TILEMAP", "DRAW3D",
    "EOF", "LOC", "LOF", "INPUT$", "DIR$",
    "LLEN", "LGETSTR$", "LGETBYTE", "LINSTR", "LCOMPARE", "LINPUT",
    "JSON$",
    NULL
};

/* built-ins that return a string (and consume a scratch buffer) */
static const char *strfuncs[] = {
    "CHR$", "LEFT$", "RIGHT$", "MID$", "STR$", "HEX$", "OCT$",
    "BIN$", "UCASE$", "LCASE$", "SPACE$", "STRING$", "LTRIM$",
    "RTRIM$", "TAB", "FORMAT$", "TRIM$", "FIELD$", "DATE$",
    "TIME$", "DATETIME$", "DAY$", "BIN2STR$", "INPUT$", "DIR$",
    "CWD$", "INKEY$", "LGETSTR$", "JSON$",
    NULL
};

/* The firmware's one-word commands (PicoMite AllCommands.h, its T_CMD
 * entries).  The tokeniser makes each a command wherever it stands, so
 * none of them is ever a variable's name; one that reaches a statement
 * as a bare name is a command we do not translate. */
static const char *fwcommands[] = {
    "ADC", "ARC", "ASTRO", "AUTOSAVE", "BACKLIGHT", "BEZIER", "BITSTREAM",
    "BLIT", "BLUETOOTH", "BOX", "CALC", "CALL", "CAMERA", "CASE", "CAT",
    "CHAIN", "CHDIR", "CIRCLE", "CLEAR", "CLOSE", "CLS", "COLOR", "COLOUR",
    "CONFIGURE", "CONST", "CONTINUE", "COPY", "CPU", "CSUB", "DATA",
    "DEFINEFONT", "DEVICE", "DIM", "DO", "DRAW3D", "DRIVE", "EDIT", "ELSE",
    "ELSEIF", "END", "ENDIF", "ERASE", "ERROR", "EXECUTE", "EXIT", "FILES",
    "FILL", "FLAGS", "FLASH", "FLUSH", "FM", "FONT", "FOR", "FRAME",
    "FRAMEBUFFER", "FUNCTION", "GAMEPAD", "GOSUB", "GOTO", "GUI", "HELP",
    "HUMID", "I2C", "I2C2", "I2CLCD", "IF", "IN", "INC", "INPUT",
    "INTERRUPT", "IR", "IRETURN", "IRQ", "JMP", "KEYBOARD", "KEYPAD",
    "KILL", "LCD", "LET", "LIBRARY", "LINE", "LIST", "LOAD", "LOCAL",
    "LOCATION", "LONGSTRING", "LOOP", "MANDELBROT", "MAP", "MATH", "MEMORY",
    "MKDIR", "MODE", "MOUSE", "MOV", "NEW", "NEXT", "NOP", "ON", "ONESHOT",
    "ONEWIRE", "OPEN", "OPTION", "OUT", "PAUSE", "PIO", "PIXEL", "PLAY",
    "POKE", "POLYGON", "PRINT", "PULL", "PULSE", "PUSH", "PWM", "RAM",
    "RANDOMIZE", "RAY", "RBOX", "READ", "REDIM", "REFRESH", "REM", "RENAME",
    "RESOLUTION", "RESTORE", "RETURN", "RMDIR", "RTC", "RUN", "SAVE",
    "SEEK", "SERVO", "SET", "SETPIN", "SETTICK", "SLEW", "SORT", "SPI",
    "SPI2", "SPRITE", "STAR", "STATIC", "STEPPER", "STRUCT", "SUB", "SYNC",
    "TEXT", "TILE", "TILEMAP", "TIMER", "TMC22XX", "TRACE", "TRIANGLE",
    "TURTLE", "TYPE", "VAR", "WAIT", "WATCHDOG", "WEB", "WHILE", "WII",
    "WS2812", "XMODEM", "YMODEM", "ZMODEM",
    NULL
};

/* The firmware's functions (AllCommands.h, its T_FUN and T_FNA entries).
 * The tokeniser matches most of them with their '(' - "Deg(" - so a name
 * followed by one is never a variable there; the T_FNA few are names on
 * their own, GETSCANLINE and the rest.  One that reaches reference() is a
 * function we do not translate.  A plain deg or pos is a variable. */
static const char *fwfunctions[] = {
    "ABS", "ACOS", "ASC", "ASIN", "ATAN2", "ATN", "BASE$", "BIN$",
    "BIN2STR$", "BIT", "BOUND", "BYTE", "CALL", "CHOICE", "CHR$",
    "CINT", "CLICK", "COS", "CTRLVAL", "DATETIME$", "DAY$", "DEG",
    "DEVICE", "DIR$", "DISTANCE", "DRAW3D", "EOF", "EPOCH", "EVAL",
    "EXP", "FIELD$", "FIX", "FLAG", "FORMAT$", "FRAME", "GPS", "HEX$",
    "INPUT$", "INSTR", "INT", "JSON$", "KEYDOWN", "LCASE$", "LCOMPARE",
    "LEFT$", "LEN", "LGETBYTE", "LGETSTR$", "LINPUT", "LINSTR", "LLEN",
    "LOC", "LOF", "LOG", "MAP", "MATH", "MAX", "MID$", "MIN", "MM.INFO",
    "MSGBOX", "PEEK", "PIN", "PIO", "PIXEL", "PORT", "PULSIN", "RAD",
    "RAY", "RGB", "RIGHT$", "RND", "SCHANGE$", "SGN", "SIN", "SPACE$",
    "SPI", "SPI2", "SPRITE", "SQR", "STR$", "STR2BIN", "STRING$",
    "STRUCT", "TAB", "TAN", "TEMPR", "TILEMAP", "TOPBOTTOM", "TOUCH",
    "TRIM$", "UCASE$", "VAL",
    NULL
};
static const char *fwfunctions_bare[] = {
    "CWD$", "DATE$", "GETSCANLINE", "INKEY$", "PI", "POS", "RND",
    "TIME$", "TIMER",
    NULL
};

/* BIN2STR$ / STR2BIN type names -> the runtime's MM_B_* constants */
static const char *bintypes[] = {
    "INT64", "UINT64", "INT32", "UINT32", "INT16", "UINT16",
    "INT8", "UINT8", "SINGLE", "DOUBLE",
    NULL
};

/* RGB() colour shortcuts, values taken from graphics/Draw.h */
static const struct { const char *name; long val; } rgbnames[] = {
    { "WHITE", 0xFFFFFF }, { "YELLOW", 0xFFFF00 }, { "LILAC", 0xFF80FF },
    { "BROWN", 0xFF8000 }, { "FUCHSIA", 0xFF40FF }, { "RUST", 0xFF4000 },
    { "MAGENTA", 0xFF00FF }, { "RED", 0xFF0000 }, { "CYAN", 0x00FFFF },
    { "GREEN", 0x00FF00 }, { "CERULEAN", 0x0080FF }, { "MIDGREEN", 0x008000 },
    { "COBALT", 0x0040FF }, { "MYRTLE", 0x004000 }, { "BLUE", 0x0000FF },
    { "BLACK", 0x000000 }, { "GRAY", 0x808080 }, { "GREY", 0x808080 },
    /* LITEGRAY is the value's name in Draw.h, not a word RGB() takes:
       fun_rgb answers "Invalid colour" to it */
    { "LIGHTGRAY", 0xD2D2D2 }, { "LIGHTGREY", 0xD2D2D2 },
    { "ORANGE", 0xFFA500 }, { "PINK", 0xFFA0AB }, { "GOLD", 0xFFD700 },
    { "SALMON", 0xFA8072 }, { "BEIGE", 0xF5F5DC },
    { NULL, 0 }
};

/* the scalar members of the MATH() family: name -> arg count */
static const struct { const char *name; int nargs; } mathfuncs[] = {
    { "COSH", 1 }, { "SINH", 1 }, { "TANH", 1 }, { "LOG10", 1 },
    { "ATAN3", 2 },
    { NULL, 0 }
};

/* the MATH() members that reduce a whole array to one number */
static const char *matharray[] = {
    "SUM", "MEAN", "SD", "MAX", "MIN", "MEDIAN",
    NULL
};

static int in_list(const char **list, const char *up)
{
    int i;
    for (i = 0; list[i]; i++)
        if (strcmp(list[i], up) == 0)
            return 1;
    return 0;
}

int kw_in(const char *up)
{
    return in_list(keywords, up);
}

const struct builtin *builtin_get(const char *up)
{
    int i;
    for (i = 0; builtins[i].name; i++)
        if (strcmp(builtins[i].name, up) == 0)
            return &builtins[i];
    return NULL;
}

/* A built-in that can be written without its brackets - PI, TIMER,
 * INKEY$ - so a name no variable can have.  DIR$ takes no argument but
 * is not one: the tokeniser matches "Dir$(" with its bracket, so a bare
 * dir$, and Elite's dir, are variables there. */
int bare_builtin(const char *up)
{
    const struct builtin *b = builtin_get(up);

    return b != NULL && b->minargs == 0 && strcmp(up, "DIR$") != 0;
}

int rawarg_in(const char *up)
{
    return in_list(rawarg, up);
}

int strfunc_in(const char *up)
{
    return in_list(strfuncs, up);
}

int fwcommand_in(const char *up)
{
    return in_list(fwcommands, up);
}

int fwfunction_in(const char *up, int paren)
{
    return in_list(paren ? fwfunctions : fwfunctions_bare, up);
}

int bintype_index(const char *up)
{
    int i;
    for (i = 0; bintypes[i]; i++)
        if (strcmp(bintypes[i], up) == 0)
            return i;
    return -1;
}

long rgbname_get(const char *up)
{
    int i;
    for (i = 0; rgbnames[i].name; i++)
        if (strcmp(rgbnames[i].name, up) == 0)
            return rgbnames[i].val;
    return -1;
}

int mathfunc_get(const char *up)
{
    int i;
    for (i = 0; mathfuncs[i].name; i++)
        if (strcmp(mathfuncs[i].name, up) == 0)
            return mathfuncs[i].nargs;
    return 0;
}

int matharray_in(const char *up)
{
    return in_list(matharray, up);
}

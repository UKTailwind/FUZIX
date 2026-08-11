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
    { "RND", 0, 1 }, { "PI", 0, 0 }, { "MAX", 2, 8 }, { "MIN", 2, 8 },
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
    { "SPI", 1, 1 },
    { "MM.HRES", 0, 0 }, { "MM.VRES", 0, 0 },
    { "MM.SPISPEED", 0, 0 },
    { "MM.ERRNO", 0, 0 }, { "MM.ERRMSG$", 0, 0 },
    { "MM.VER", 0, 0 }, { "MM.DEVICE$", 0, 0 },
    { "MM.CMDLINE$", 0, 0 },
    { "DIR$", 0, 2 },
    { "LLEN", 1, 1 }, { "LGETSTR$", 3, 3 }, { "LGETBYTE", 2, 2 },
    { "LINSTR", 2, 3 }, { "LCOMPARE", 2, 2 }, { "LINPUT", 3, 3 },
    { NULL, 0, 0 }
};

/* built-ins whose arguments cannot be parsed as plain expressions */
static const char *rawarg[] = {
    "CHOICE", "BOUND", "TRIM$", "DATETIME$", "DAY$", "EPOCH",
    "BIN2STR$", "STR2BIN", "RGB", "MATH",
    "EOF", "LOC", "LOF", "INPUT$", "DIR$",
    "LLEN", "LGETSTR$", "LGETBYTE", "LINSTR", "LCOMPARE", "LINPUT",
    NULL
};

/* built-ins that return a string (and consume a scratch buffer) */
static const char *strfuncs[] = {
    "CHR$", "LEFT$", "RIGHT$", "MID$", "STR$", "HEX$", "OCT$",
    "BIN$", "UCASE$", "LCASE$", "SPACE$", "STRING$", "LTRIM$",
    "RTRIM$", "TAB", "FORMAT$", "TRIM$", "FIELD$", "DATE$",
    "TIME$", "DATETIME$", "DAY$", "BIN2STR$", "INPUT$", "DIR$",
    "CWD$", "INKEY$", "LGETSTR$",
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
    { "LITEGRAY", 0xD2D2D2 }, { "LIGHTGRAY", 0xD2D2D2 },
    { "LIGHTGREY", 0xD2D2D2 },
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

int rawarg_in(const char *up)
{
    return in_list(rawarg, up);
}

int strfunc_in(const char *up)
{
    return in_list(strfuncs, up);
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

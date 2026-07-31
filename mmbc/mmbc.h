/*
 * mmbc.h - mmbc, the mmb2c translator in C.
 *
 * A function-for-function mirror of mmb2c.py; the Python is the
 * reference implementation and byte-identical output over the test
 * suite (mmbc/mmbctests.sh) is the definition of correct.  Where this
 * file or its .c files do something odd, the answer is usually "that
 * is what the Python does" - check mmb2c.py before "fixing" it.
 *
 * Memory model: two bump pools.  The scratch pool holds token texts
 * and expression fragments and is reset at the top of tokenize() -
 * one line at a time, so usage is bounded on a 256K Fuzix process.
 * Anything that outlives the current line (symbol names, block-stack
 * texts, output lines) must be copied out with pstr() at the point of
 * store.  The persistent pool is never freed, like the Python.
 */

#ifndef MMBC_H
#define MMBC_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>

#define VERSION "0.1"

/* ---- types (mmb2c.py TY_F / TY_I / TY_S) ---- */

#define TY_F 'f'                /* MMFLOAT   (double)  - MMBasic default */
#define TY_I 'i'                /* MMINTEGER (int64_t) */
#define TY_S 's'                /* string, MMBasic layout [len][data][NUL] */
#define TY_NONE 0               /* Python None */

const char *ctype_of(int ty);   /* CTYPE[]  */
const char *tyname_of(int ty);  /* TYNAME[] */

/* ---- tokens ---- */

#define T_ID 1
#define T_NUM 2
#define T_STR 3
#define T_OP 4

struct tok {
    int kind;
    const char *text;           /* scratch lifetime - pstr() before storing */
    const char *up;             /* T_ID: upper-cased; T_NUM: "F"/"I"/"H";
                                   T_STR: ""; T_OP: canonical op text */
};

/* Python builds unbounded lists; a fixed cap with a hard error is the
 * honest C translation - only reachable on lines no real program has. */
#define MAXTOKS 512

int tokenize(const char *line, int lineno, struct tok *out);

/* ---- string helpers (mmbc_lex.c) ---- */

int is_alpha(int c);
int is_digit_c(int c);          /* is_digit clashes with some libcs */
int is_idchar(int c);
int is_hexd(int c);
char *cblock_safe(const char *text);
char *c_string_literal(const char *s);
char *split_suffix(const char *word, int *ty);  /* ty out: TY_* or TY_NONE */
char *cvar(const char *name);
char *clabel(const char *name);
char *upper(const char *s);
char *lower(const char *s);

/* ---- pools + formatting (mmbc_util.c) ---- */

void *palloc(size_t n);
char *pstr(const char *s);
void *salloc(size_t n);
char *sstr(const char *s);
void scratch_reset(void);
char *sfmt(const char *fmt, ...);   /* scratch sprintf - the workhorse
                                       standing in for Python % */

/* ---- MMError (mmbc_util.c) ----
 *
 * mm_error() formats err_msg and longjmps to the current catch frame,
 * mirroring `raise MMError`.  Catch sites push a frame:
 *
 *     jmp_buf jb, *saved = err_jmp;
 *     err_jmp = &jb;
 *     if (setjmp(jb) == 0) { ...protected...; err_jmp = saved; }
 *     else { err_jmp = saved; ...handler, message in err_msg...; }
 *
 * With no frame in place mm_error prints and exits, standing in for an
 * uncaught Python traceback.
 */

extern jmp_buf *err_jmp;
extern char err_msg[512];
void mm_error(const char *fmt, ...);

/* ---- source lines (mmbc_main.c) ---- */

extern char **src_lines;        /* with trailing \n kept, like readline() */
extern int src_nlines;

#endif

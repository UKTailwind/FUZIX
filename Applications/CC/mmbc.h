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
int const_c_expr(const char *text);
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

/* ---- static word tables (mmbc_tab.c) ---- */

struct builtin { const char *name; int minargs, maxargs; };

int kw_in(const char *up);                  /* up in KEYWORDS */
const struct builtin *builtin_get(const char *up);   /* BUILTINS[up] */
int rawarg_in(const char *up);
int strfunc_in(const char *up);
int bintype_index(const char *up);          /* BINTYPES.index, -1 absent */
long rgbname_get(const char *up);           /* RGBNAMES[up], -1 absent */
int mathfunc_get(const char *up);           /* MATHFUNCS[up] argc, 0 absent */
int matharray_in(const char *up);

/* ---- values: what every expression routine returns ---- */

struct val { const char *code; int ty; };

/* ---- symbols ---- */

struct sym {
    const char *name;           /* canonical: lower case, no suffix */
    const char *disp;           /* as the programmer spelled it */
    int ty;
    const char *acc;            /* C text used to read/write it */
    const char **dims;          /* C size expressions */
    int ndims;
    int is_const, is_array, is_param, byref, is_static;
    int where;                  /* source line first seen */
    int implied;
    int has_init;
    const char *declared_in;    /* "" = main line, else routine name */
};

struct gtouch { const char *name; int line; };

struct routine {
    const char *name;           /* canonical */
    const char *disp;
    const char *cname;          /* f_<name> */
    int is_func;
    int ty;
    struct sym **params; int nparams, cparams;
    struct sym **locals; int nlocals, clocals;   /* insertion-ordered map */
    const char **local_order; int nlocal_order, clocal_order;
    struct sym **statics; int nstatics, cstatics;
    int line;
    struct gtouch *gtouch; int ngtouch, cgtouch; /* global -> first line */
    int heap_locals;            /* has LOCAL arrays or strings, so its
                                 * invocations carry a heap block */
};

/* One record per label name; the Python keeps six dicts keyed the same
 * way (labels, data_at, label_routine, label_depth, goto_depth,
 * labels_used), so each field carries its own "key present" flag to
 * keep dict-membership semantics exact. */
struct label {
    const char *name;
    int placed, lineno;         /* labels{} */
    int has_data_at, data_at;   /* data_at{} */
    const char *routine;        /* label_routine{}, NULL = absent */
    int has_depth, depth;       /* label_depth{} */
    int has_goto, goto_depth;   /* goto_depth{} */
    int used;                   /* labels_used{} */
};

struct implied_rec { const char *name; int ty; int line;
                     const char *routine; };
struct data_item { int kind; const char *f, *i, *sv; };
struct skip_rec { int line; const char *text; const char *why; };
struct bnd { const char *key; const char *name; const char *body; };

struct outbuf { const char **lines; int n, cap; };

/* gosub_sites: routine name ('' = main) -> site numbers, scan order */
struct gsub { const char *routine; int *sites; int n, cap; };

/* The Python block stack holds tuples of per-kind shape; tuple[0] is
 * the kind ('if', 'for', 'do', 'while', 'select', 'routine', ...) and
 * the LAST element is always the start line (walk's unterminated-block
 * error reads blk[0] and blk[-1]).  The middle members become a..d;
 * each push site documents its own mapping. */
/* Shapes: ['if',line] ['while',line] ['routine',line]
 * ['for',canon,line]->a  ['do','head'|'tail',line]->a
 * ['select',name,ty,line]->a,ty.  Strings pushed here must be pstr()d
 * (newtmp returns scratch). */
struct block { const char *kind; const char *a, *b, *c, *d;
               int ty; int line; };

/* ---- the guts: Python's Conv instance as one global ---- */

#define M_SCAN 0
#define M_DECL 1
#define M_EMIT 2

struct conv {
    const char *srcname;
    struct sym **globals; int nglobals, cglobals;      /* insertion order */
    struct routine **routines; int nroutines, croutines;
    const char **routine_names; int nroutine_names, croutine_names;
    struct label *labels; int nlabels, clabels;
    const char **errors; int nerrors, cerrors;
    const char **warnings; int nwarnings, cwarnings;
    struct implied_rec *implied; int nimplied, cimplied;
    struct data_item *data; int ndata, cdata;
    int lenient, fcc;
    struct bnd *bnds; int nbnds, cbnds;                /* bnd_tables */
    struct skip_rec *skipped; int nskipped, cskipped;
    struct gsub *gsubs; int ngsubs, cgsubs;            /* gosub_sites */
    int gosub_n;
    int opt_default;            /* TY_* or TY_NONE (OPTION DEFAULT NONE) */
    int opt_explicit, opt_base;
    int mode;
    struct routine *cur;        /* NULL = main line */
    struct outbuf out_main, out_body;
    struct outbuf *out;
    int indent;
    struct block *blocks; int nblocks, cblocks;
    int tmpn;
    int lineno;
    struct tok toks[MAXTOKS]; int ntoks;
    int i;
    int tmp_used;
    /* depth of single-line IF bodies being emitted: END SUB means
     * "return now" in there, not "the routine ends here" */
    int inline_depth;
    int uses_clear;
    int uses_gfx;
    /* set by global_decls when the program has any array or string:
     * main() then has to allocate the block they live in */
    int heap_used;
};

extern struct conv cv;

/* ---- growable arrays ---- */

void *xrealloc(void *p, size_t n);
#define GROW(arr, n, cap) do { \
        if ((n) == (cap)) { \
            (cap) = (cap) ? (cap) * 2 : 16; \
            (arr) = xrealloc((arr), sizeof(*(arr)) * (size_t)(cap)); \
        } \
    } while (0)

/* ---- conv plumbing (mmbc_sym.c) ---- */

void cv_err(const char *fmt, ...);          /* self.err - raises */
void cv_note(const char *fmt, ...);         /* self.note - records, no raise */
void cv_warn(const char *fmt, ...);         /* self.warn - dedupes */
struct tok *peek(int k);
int at_end(void);
struct tok *nxt(void);
int is_op(const char *s, int k);
int is_kw(const char *s, int k);
int accept_op(const char *s);
void expect_op(const char *s);
int accept_kw(const char *s);
int stmt_end(void);
void emit(const char *text);
void raw(const char *text);
char *newtmp(const char *pfx);
void out_append(struct outbuf *o, const char *persistent_line);
void out_insert(struct outbuf *o, int where, const char *persistent_line);
struct label *label_rec(const char *canon);  /* find-or-create */
struct sym *sym_lookup(const char *canon);   /* self.lookup */
struct sym *declare(const char *canon, int ty, const char *scope,
                    const char **arr_dims, int ndims, int is_static);
struct sym *reference(const char *word, int as_array);
void note_touch(const char *canon, struct sym *s);
const char *as_int(struct val v);
const char *as_flt(struct val v);
struct val need_num(struct val v);
void errors_add(const char *msg);            /* append, no dedupe */
void errors_add_dedup(const char *msg);      /* the statement-site form */

/* ---- expression grammar (mmbc_expr.c / mmbc_builtin.c) ----
 * Full surface (e_* chain, call machinery, builtins) in mmbc_expr.h;
 * only the entry point is shared here. */

struct val expr(void);

/* ---- declaration passes (mmbc_decl.c) ---- */

void pass_routine_names(void);
void pass_declarations(void);
void place_label(const char *canon);
void strip_line_number(void);
void skip_statement(void);
void decl_statement(void);
void collect_data(void);
char *source_text(int a, int b);
void decl_routine(int is_func);
void decl_param(struct routine *r);
void do_option(void);
void do_declare(const char *kw);
void do_const(void);

/* ---- statement walk (mmbc_walk.c / mmbc_stmt.c) ---- */

void walk(int mode);
void statement(void);
void statement_inner(void);
char *loop_cond(const char *c);
int is_literal_number(struct val v);
struct routine *routine_get(const char *canon);
int type_word(void);
char *zero_of(struct sym *s);
char *signature(struct routine *r);

/* ---- output side (mmbc_out.c) ---- */

void report_build(struct outbuf *o);
void conv_write(FILE *f);

#endif

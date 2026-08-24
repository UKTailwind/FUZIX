/*
 * mmbc_expr.h - the expression grammar surface (mmbc_expr.c) and the
 * built-in function machinery (mmbc_builtin.c).
 *
 * Mirrors mmb2c.py `def expr` (576) through `def bound_of` (1371-1395).
 * expr() itself is prototyped in mmbc.h; everything else the statement
 * region reaches for is here.
 */

#ifndef MMBC_EXPR_H
#define MMBC_EXPR_H

#include "mmbc.h"

/* ---- argument lists (call_args / arg_item / emit_call / pass_arg) ----
 *
 * The Python builds a list whose items are
 *     None                        - omitted argument
 *     ('array', sym, None)        - whole array:  a()
 *     ('var',   sym, None)        - bare scalar variable
 *     ('elem',  sym, (code, ty))  - one element:  a(i)
 *     ('val',   None, (code, ty)) - any other expression
 * An omitted item becomes kind ARG_NONE; emit_call also passes NULL
 * for a position beyond the end of the list (Python None either way,
 * and pass_arg treats the two identically). */

#define ARG_NONE  0
#define ARG_ARRAY 1
#define ARG_VAR   2
#define ARG_VAL   3
#define ARG_ELEM  4

struct arg {
    int kind;
    struct sym *s;              /* ARG_ARRAY / ARG_VAR / ARG_ELEM */
    struct val v;               /* ARG_VAL / ARG_ELEM */
};

/* Python lists are unbounded; a fixed cap with a hard error is the
 * honest C translation - only reachable on calls no real program has. */
#define MAXARGS 64

struct arglist { struct arg a[MAXARGS]; int n; };

/* (pointer to element 0, element count) - array_flat/lsref's tuple */
struct flat { const char *ptr; const char *cnt; };

/* ---- expression grammar (mmbc_expr.c) ---- */

struct val e_logical(void);
struct val e_compare(void);
struct val e_unary_not(void);
struct val e_shift(void);
struct val e_add(void);
struct val e_mul(void);
struct val e_unary(void);
struct val e_pow(void);
struct val e_primary(void);
struct val e_name(void);

/* Python `index` - renamed: index() clashes with libc's strings.h */
const char *index_of(struct sym *s);
const char *retacc(void);

void call_args(int need_parens, struct arglist *out);
void arg_item(struct arg *out);
struct val emit_call(struct routine *r, struct arglist *args);
const char *pass_arg(struct sym *p, struct arg *a, struct routine *r);

struct sym *arrayref(int need_parens);      /* Python default: True */
int is_array_arg(void);                     /* does "a()" start here? */
struct flat array_flat(struct sym *s);
/* one line through an array: where it starts, how far apart its
   elements are, and how many there are (ARRAY SLICE / ARRAY INSERT) */
struct vec { const char *ptr; const char *step; const char *cnt; };
const char *subscript_of(struct sym *s, const char **parts, int nparts);
const char *bnd_acc(struct sym *s);
const char *usable(const char *txt);
const char *count_of(const char *bound);
const char *rebase(const char *part);
const char *unbase(const char *txt);
void dim_sizes(struct sym *s, int rank, const char **out);
struct flat array_line(struct sym *s);
struct plane { const char *ptr; const char *nc; const char *nr;
               const char *stride; };
struct plane array_plane(struct sym *s);
struct wvar { const char *acc; int ty; };
struct wvar window_var(void);
struct vec array_vector(struct sym *s, const char **parts, int nparts,
                        int blank);
struct flat lsref(void);
const char *channel(void);
/* dim: Python None -> has_dim = 0 (dim ignored) */
const char *bound_of(struct sym *sym, struct val dim, int has_dim);

/* ---- built-ins (mmbc_builtin.c) ---- */

struct val call_builtin(const char *up);
struct val emit_builtin(const char *up, struct val *args, int nargs);
struct val builtin_raw(const char *up);

#endif

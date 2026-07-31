/* mmbc_expr.c - the expression grammar: the e_* chain, names and
 * subscripts, and the user SUB/FUNCTION call machinery.
 *
 * Mirrors mmb2c.py `def expr` (576) through `def pass_arg` (858-931)
 * plus arrayref/array_flat/lsref/channel/bound_of (1328-1395); the
 * built-in dispatch between them lives in mmbc_builtin.c.  Generated
 * text and error messages are byte identity - keep every character. */

#include "mmbc.h"
#include "mmbc_expr.h"

/* mmbc_decl.c */
struct routine *routine_get(const char *canon);

/* mmbc_stmt.c (Python line 2910) */ /* TODO integrate */
int is_literal_number(struct val v);

static struct val mkval(const char *code, int ty)
{
    struct val v;
    v.code = code;
    v.ty = ty;
    return v;
}

/* '__b_' + name.replace('.', '__') - the hidden bounds argument that
 * travels alongside an array parameter. */
static char *bname(const char *name)
{
    size_t n = strlen(name);
    char *out = salloc(4 + n * 2 + 1);
    size_t j = 4;
    size_t i;

    memcpy(out, "__b_", 4);
    for (i = 0; i < n; i++) {
        if (name[i] == '.') {
            out[j++] = '_';
            out[j++] = '_';
        } else {
            out[j++] = name[i];
        }
    }
    out[j] = 0;
    return out;
}

struct val expr(void)
{
    return e_logical();
}

struct val e_logical(void)
{
    struct val v = e_compare();

    for (;;) {
        struct tok *t = peek(0);
        const char *op;
        const char *cop;
        const char *a;
        const char *b;
        struct val r;

        if (t == NULL || t->kind != T_ID
            || (strcmp(t->up, "AND") != 0 && strcmp(t->up, "OR") != 0
                && strcmp(t->up, "XOR") != 0))
            return v;
        op = t->up;
        cv.i += 1;
        r = e_compare();
        a = as_int(v);
        b = as_int(r);
        cop = strcmp(op, "AND") == 0 ? "&"
            : strcmp(op, "OR") == 0 ? "|" : "^";
        v = mkval(sfmt("(%s %s %s)", a, cop, b), TY_I);
    }
}

struct val e_compare(void)
{
    struct val v = e_unary_not();

    for (;;) {
        struct tok *t = peek(0);
        const char *op;
        const char *cop;
        struct val r;

        if (t == NULL || t->kind != T_OP)
            return v;
        op = t->text;
        if (strcmp(op, "=") != 0 && strcmp(op, "<>") != 0
            && strcmp(op, "<") != 0 && strcmp(op, ">") != 0
            && strcmp(op, "<=") != 0 && strcmp(op, ">=") != 0)
            return v;
        cv.i += 1;
        r = e_unary_not();
        cop = strcmp(op, "=") == 0 ? "=="
            : strcmp(op, "<>") == 0 ? "!=" : op;
        if (v.ty == TY_S || r.ty == TY_S) {
            if (v.ty != TY_S || r.ty != TY_S)
                cv_err("cannot compare a string with a number");
            v = mkval(sfmt("(mm_scmp(%s, %s) %s 0)", v.code, r.code, cop),
                      TY_I);
        } else {
            v = mkval(sfmt("((%s) %s (%s) ? 1 : 0)", v.code, cop, r.code),
                      TY_I);
        }
    }
}

struct val e_unary_not(void)
{
    struct tok *t = peek(0);

    if (t != NULL && t->kind == T_ID
        && (strcmp(t->up, "NOT") == 0 || strcmp(t->up, "INV") == 0)) {
        struct val v;

        cv.i += 1;
        v = e_unary_not();
        if (strcmp(t->up, "NOT") == 0)
            return mkval(sfmt("((%s) == 0 ? 1 : 0)", as_flt(v)), TY_I);
        return mkval(sfmt("(~(%s))", as_int(v)), TY_I);
    }
    return e_shift();
}

struct val e_shift(void)
{
    struct val v = e_add();

    while (is_op("<<", 0) || is_op(">>", 0)) {
        const char *op = nxt()->text;
        struct val r = e_add();
        v = mkval(sfmt("((%s) %s (%s))", as_int(v), op, as_int(r)), TY_I);
    }
    return v;
}

struct val e_add(void)
{
    struct val v = e_mul();

    while (is_op("+", 0) || is_op("-", 0)) {
        const char *op = nxt()->text;
        struct val r = e_mul();

        if (v.ty == TY_S || r.ty == TY_S) {
            if (strcmp(op, "+") != 0 || v.ty != TY_S || r.ty != TY_S)
                cv_err("bad string operation");
            cv.tmp_used = 1;
            v = mkval(sfmt("mm_scat(%s, %s)", v.code, r.code), TY_S);
        } else if (v.ty == TY_I && r.ty == TY_I) {
            v = mkval(sfmt("((%s) %s (%s))", v.code, op, r.code), TY_I);
        } else {
            v = mkval(sfmt("((%s) %s (%s))", as_flt(v), op, as_flt(r)),
                      TY_F);
        }
    }
    return v;
}

struct val e_mul(void)
{
    struct val v = e_unary();

    for (;;) {
        struct tok *t = peek(0);

        if (t == NULL)
            return v;
        if (t->kind == T_OP && (strcmp(t->text, "*") == 0
                                || strcmp(t->text, "/") == 0
                                || strcmp(t->text, "\\") == 0)) {
            const char *op = nxt()->text;
            struct val r = e_unary();

            if (strcmp(op, "/") == 0) {
                v = mkval(sfmt("((%s) / (%s))", as_flt(v), as_flt(r)),
                          TY_F);
            } else if (strcmp(op, "\\") == 0) {
                v = mkval(sfmt("mm_idiv(%s, %s)", as_int(v), as_int(r)),
                          TY_I);
            } else if (v.ty == TY_I && r.ty == TY_I) {
                v = mkval(sfmt("((%s) * (%s))", v.code, r.code), TY_I);
            } else {
                v = mkval(sfmt("((%s) * (%s))", as_flt(v), as_flt(r)),
                          TY_F);
            }
        } else if (t->kind == T_ID && strcmp(t->up, "MOD") == 0) {
            struct val r;

            cv.i += 1;
            r = e_unary();
            v = mkval(sfmt("mm_mod(%s, %s)", as_int(v), as_int(r)), TY_I);
        } else {
            return v;
        }
    }
}

struct val e_unary(void)
{
    if (is_op("-", 0)) {
        struct val v;

        cv.i += 1;
        v = need_num(e_unary());
        return mkval(sfmt("(-(%s))", v.code), v.ty);
    }
    if (is_op("+", 0)) {
        cv.i += 1;
        return e_unary();
    }
    return e_pow();
}

struct val e_pow(void)
{
    struct val v = e_primary();

    if (is_op("^", 0)) {
        struct val r;

        cv.i += 1;
        r = e_unary();
        return mkval(sfmt("mm_pow(%s, %s)", as_flt(v), as_flt(r)), TY_F);
    }
    return v;
}

struct val e_primary(void)
{
    struct tok *t = peek(0);

    if (t == NULL)
        cv_err("expression expected");
    if (t->kind == T_NUM) {
        cv.i += 1;
        if (strcmp(t->up, "F") == 0)
            return mkval((strchr(t->text, '.') != NULL
                          || strchr(t->text, 'e') != NULL
                          || strchr(t->text, 'E') != NULL)
                         ? t->text : sfmt("%s.0", t->text), TY_F);
        if (strcmp(t->up, "H") == 0)
            return mkval(t->text, TY_I);
        return mkval(sfmt("%sLL", t->text), TY_I);
    }
    if (t->kind == T_STR) {
        cv.i += 1;
        return mkval(c_string_literal(t->text), TY_S);
    }
    if (t->kind == T_OP && strcmp(t->text, "(") == 0) {
        struct val v;

        cv.i += 1;
        v = expr();
        expect_op(")");
        return mkval(sfmt("(%s)", v.code), v.ty);
    }
    if (t->kind == T_ID)
        return e_name();
    cv_err("unexpected '%s'", t->text);
    return mkval(NULL, TY_NONE);        /* not reached */
}

/* -- a name in an expression --------------------------------------- */
struct val e_name(void)
{
    struct tok *t = nxt();
    const char *word = t->text;
    const char *up = t->up;
    int sfx;
    char *canon = split_suffix(word, &sfx);
    struct routine *r;
    const struct builtin *b;
    int as_array;
    struct sym *s;

    (void)sfx;                          /* the Python discards it too */

    /* the current function's own name = its return value */
    if (cv.cur != NULL && cv.cur->is_func
        && strcmp(canon, cv.cur->name) == 0 && !is_op("(", 0))
        return mkval(retacc(), cv.cur->ty);

    /* a user defined SUB/FUNCTION always wins over a built-in of the
     * same name - the manual's own examples define Trim$(), and a
     * program written before a built-in existed must keep working */
    r = routine_get(canon);
    if (r != NULL) {
        struct arglist args;

        if (!r->is_func)
            cv_err("'%s' is a SUB, not a FUNCTION", canon);
        if (builtin_get(up) != NULL)
            cv_warn("'%s' is also a built-in function; the version "
                    "defined in this program is being used", t->text);
        call_args(1, &args);
        return emit_call(r, &args);
    }

    b = builtin_get(up);
    if (b != NULL && (b->minargs == 0 || is_op("(", 0)))
        return call_builtin(up);

    as_array = is_op("(", 0);
    s = reference(word, as_array);
    if (as_array) {
        if (!s->is_array)
            cv_err("'%s' is not an array", canon);
        return mkval(index_of(s), s->ty);
    }
    if (s->is_const)
        return mkval(s->acc, s->ty);
    if (s->is_array)
        cv_err("array '%s' used without an index", canon);
    return mkval(s->acc, s->ty);
}

/* Consume ( i [, j ...] ) and build the C subscript.
 * Python `index` - renamed: index() clashes with libc's strings.h. */
const char *index_of(struct sym *s)
{
    const char *parts[MAXARGS];
    int nparts = 0;
    const char *res;
    int k;

    expect_op("(");
    for (;;) {
        struct val v = expr();

        if (nparts >= MAXARGS)
            mm_error("line %d: too many subscripts", cv.lineno);
        parts[nparts++] = sfmt("(int)(%s)", as_int(v));
        if (!accept_op(","))
            break;
    }
    expect_op(")");
    if (s->is_param) {
        /* MMBasic gives an array parameter no rank of its own - it
         * inherits whatever was passed - so the subscripts are folded
         * into one offset using the bounds handed in alongside it. */
        const char *b = bname(s->name);
        const char *off = parts[0];

        for (k = 1; k < nparts; k++)
            off = sfmt("((%s) * ((%s)[%d] + 1) + (%s))", off, b, k + 1,
                       parts[k]);
        return sfmt("%s[%s]", s->acc, off);
    }
    if (nparts != s->ndims)
        cv_err("'%s' has %d dimension(s), %d given",
               s->name, s->ndims, nparts);
    res = s->acc;
    for (k = 0; k < nparts; k++)
        res = sfmt("%s[%s]", res, parts[k]);
    return res;
}

const char *retacc(void)
{
    return "__ret";
}

/* -- argument lists ------------------------------------------------- */

/* Fills *out with the items described in mmbc_expr.h. */
void call_args(int need_parens, struct arglist *out)
{
    out->n = 0;
    if (need_parens) {
        expect_op("(");
        if (accept_op(")"))
            return;
    } else {
        if (stmt_end())
            return;
        if (accept_op("(")) {
            /* sub call written with brackets: SUB(a, b) */
            if (accept_op(")"))
                return;
            need_parens = 1;
        }
    }
    for (;;) {
        if (out->n >= MAXARGS)
            mm_error("line %d: too many arguments", cv.lineno);
        if (is_op(",", 0) || (need_parens && is_op(")", 0))
            || (!need_parens && stmt_end())) {
            out->a[out->n].kind = ARG_NONE;
            out->a[out->n].s = NULL;
            out->a[out->n].v = mkval(NULL, TY_NONE);
            out->n++;
        } else {
            arg_item(&out->a[out->n]);
            out->n++;
        }
        if (accept_op(","))
            continue;
        break;
    }
    if (need_parens)
        expect_op(")");
}

/* One actual argument.  Detect the bare-variable and whole-array
 * forms so that they can be passed by reference. */
void arg_item(struct arg *out)
{
    struct tok *t = peek(0);

    out->s = NULL;
    out->v = mkval(NULL, TY_NONE);
    if (t != NULL && t->kind == T_ID && builtin_get(t->up) == NULL) {
        int sfx;
        char *canon = split_suffix(t->text, &sfx);

        (void)sfx;
        if (routine_get(canon) == NULL) {
            struct tok *nxt1 = peek(1);
            int after_is_end;

            /* whole array:  a() */
            if (nxt1 != NULL && nxt1->kind == T_OP
                && strcmp(nxt1->text, "(") == 0 && is_op(")", 2)) {
                cv.i += 3;
                out->kind = ARG_ARRAY;
                out->s = reference(t->text, 1);
                return;
            }
            /* bare scalar variable */
            after_is_end = (nxt1 == NULL
                            || (nxt1->kind == T_OP
                                && (strcmp(nxt1->text, ",") == 0
                                    || strcmp(nxt1->text, ")") == 0
                                    || strcmp(nxt1->text, ":") == 0)));
            if (after_is_end) {
                struct sym *s = sym_lookup(canon);

                if (s == NULL)
                    s = reference(t->text, 0);
                if (!s->is_const && !s->is_array) {
                    cv.i += 1;
                    out->kind = ARG_VAR;
                    out->s = s;
                    return;
                }
            }
        }
    }
    out->kind = ARG_VAL;
    out->v = expr();
}

/* Build the C call text for a user SUB or FUNCTION. */
struct val emit_call(struct routine *r, struct arglist *args)
{
    const char *joined = NULL;
    int k;

    if (args->n > r->nparams)
        cv_err("too many arguments to '%s'", r->name);
    if (r->is_func && r->ty == TY_S) {
        cv.tmp_used = 1;
        joined = "mm_tmp()";
    }
    for (k = 0; k < r->nparams; k++) {
        struct arg *a = (k < args->n) ? &args->a[k] : NULL;
        const char *piece = pass_arg(r->params[k], a, r);

        joined = joined ? sfmt("%s, %s", joined, piece) : piece;
    }
    return mkval(sfmt("%s(%s)", r->cname, joined ? joined : ""),
                 r->is_func ? r->ty : TY_NONE);
}

/* a == NULL (or kind ARG_NONE): the Python's None. */
const char *pass_arg(struct sym *p, struct arg *a, struct routine *r)
{
    if (a != NULL && a->kind == ARG_NONE)
        a = NULL;
    if (p->is_array) {
        struct sym *s;
        const char *bnd;
        const char *base;

        if (a == NULL)
            cv_err("array argument to '%s' cannot be omitted", r->name);
        if (a->kind != ARG_ARRAY)
            cv_err("'%s' expects a whole array here", r->name);
        s = a->s;
        if (s->ty != p->ty)
            cv_err("array type mismatch in call to '%s'", r->name);
        if (s->is_param) {
            bnd = bname(s->name);
            base = s->acc;
        } else {
            const char *dj = NULL;
            const char *body;
            int k;

            for (k = 0; k < s->ndims; k++)
                dj = dj ? sfmt("%s, (%s) - 1", dj, s->dims[k])
                        : sfmt("(%s) - 1", s->dims[k]);
            body = sfmt("%d, %s", s->ndims, dj ? dj : "");
            if (cv.fcc) {
                /* FCC has no compound literals; the contents are
                 * compile-time constant, so hoist one static table
                 * per array to file scope instead. */
                struct bnd *bt = NULL;

                for (k = 0; k < cv.nbnds; k++)
                    if (strcmp(cv.bnds[k].key, s->acc) == 0) {
                        bt = &cv.bnds[k];
                        break;
                    }
                if (bt == NULL) {
                    GROW(cv.bnds, cv.nbnds, cv.cbnds);
                    bt = &cv.bnds[cv.nbnds];
                    bt->key = pstr(s->acc);
                    bt->name = pstr(sfmt("__bnd_%d", cv.nbnds));
                    bt->body = pstr(body);
                    cv.nbnds++;
                }
                bnd = bt->name;
            } else {
                bnd = sfmt("(const MMINTEGER[]){ %s }", body);
            }
            /* flatten, so the callee can index any rank it likes */
            if (s->ty == TY_S)
                base = sfmt("(char (*)[MM_STRSZ])%s", s->acc);
            else
                base = sfmt("(%s *)%s", ctype_of(s->ty), s->acc);
        }
        return sfmt("%s, %s", base, bnd);
    }
    if (p->ty == TY_S) {
        struct val v;

        if (a == NULL)
            return "mm_tmp()";
        if (a->kind == ARG_VAR) {
            if (a->s->ty != TY_S)
                cv_err("type mismatch in call to '%s'", r->name);
            if (p->byref)
                return a->s->acc;
            cv.tmp_used = 1;
            return sfmt("mm_scopy(%s)", a->s->acc);
        }
        if (a->kind == ARG_ARRAY)
            cv_err("unexpected array argument");
        v = a->v;
        if (v.ty != TY_S)
            cv_err("type mismatch in call to '%s'", r->name);
        if (v.code[0] == '"') {
            /* a literal is not writable: give the callee a scratch copy */
            cv.tmp_used = 1;
            return sfmt("mm_scopy(%s)", v.code);
        }
        return v.code;
    }
    {
        const char *ct = ctype_of(p->ty);
        const char *val = NULL;

        if (a == NULL) {
            val = "0";
        } else if (a->kind == ARG_VAR) {
            struct val av;

            if (a->s->ty == p->ty && p->byref)
                return sfmt("&%s", a->s->acc);
            av = mkval(a->s->acc, a->s->ty);
            val = (p->ty == TY_I) ? as_int(av) : as_flt(av);
        } else if (a->kind == ARG_ARRAY) {
            cv_err("unexpected array argument");
        } else {
            val = (p->ty == TY_I) ? as_int(a->v) : as_flt(a->v);
        }
        if (p->byref) {
            if (cv.fcc)
                /* No compound literals in FCC: the runtime parks the
                 * value in a small ring of scratch slots and returns
                 * its address. */
                return sfmt("mm_byref_%s(%s)",
                            (p->ty == TY_I) ? "i" : "f", val);
            return sfmt("(%s[]){ %s }", ct, val);
        }
        return sfmt("(%s)", val);
    }
}

/* A whole array, written a() or (for ERASE) just a.
 * need_parens: Python default True. */
struct sym *arrayref(int need_parens)
{
    struct tok *t = nxt();
    struct sym *sym;

    if (t->kind != T_ID)
        cv_err("an array name was expected");
    sym = reference(t->text, is_op("(", 0));
    if (accept_op("(")) {
        expect_op(")");
    } else if (need_parens) {
        cv_err("'%s' should be written %s()", t->text, t->text);
    }
    return sym;
}

/* (pointer to element 0, element count) for a whole array. */
struct flat array_flat(struct sym *s)
{
    struct flat r;

    if (!s->is_array)
        cv_err("'%s' is not an array", s->name);
    if (s->is_param) {
        r.ptr = s->acc;
        r.cnt = sfmt("mm_arr_count(%s)", bname(s->name));
        return r;
    }
    {
        const char *j = NULL;
        int k;

        for (k = 0; k < s->ndims; k++)
            j = j ? sfmt("%s * (%s)", j, s->dims[k])
                  : sfmt("(%s)", s->dims[k]);
        r.cnt = sfmt("(int)(%s)", j ? j : "");
    }
    if (s->ty == TY_S)
        r.ptr = sfmt("(char (*)[MM_STRSZ])%s", s->acc);
    else
        r.ptr = sfmt("(%s *)%s", ctype_of(s->ty), s->acc);
    return r;
}

/* A long string: an INTEGER array holding the byte count in
 * element 0 and the payload from element 1 on. */
struct flat lsref(void)
{
    struct sym *sym = arrayref(1);

    if (sym->ty != TY_I)
        cv_err("'%s' is not an integer array, so it cannot hold a "
               "long string", sym->name);
    if (sym->ndims != 1)
        cv_err("a long string must be a one-dimensional array");
    return array_flat(sym);
}

/* A file number, with the '#' optional as it is in MMBasic. */
const char *channel(void)
{
    struct val v;

    accept_op("#");
    v = expr();
    if (v.ty == TY_S)
        cv_err("a file number must be a number");
    return as_int(v);
}

/* BOUND() resolves at compile time for a real array; an array
 * parameter carries its bounds in a hidden extra argument.
 * dim: Python None -> has_dim = 0. */
const char *bound_of(struct sym *sym, struct val dim, int has_dim)
{
    long k = 0;
    int has_k;
    const char *kexpr;

    if (!has_dim) {
        k = 1;                  /* "defaults to one if not specified" */
        has_k = 1;
        kexpr = "1";
    } else if (is_literal_number(dim)) {
        /* int(code.replace('LL', '').replace('(', '').replace(')', '')
         *     .split('.')[0]) */
        char *stripped = salloc(strlen(dim.code) + 1);
        const char *p = dim.code;
        size_t j = 0;

        while (*p) {
            if (p[0] == 'L' && p[1] == 'L') {
                p += 2;
                continue;
            }
            if (*p == '(' || *p == ')') {
                p++;
                continue;
            }
            if (*p == '.')
                break;
            stripped[j++] = *p++;
        }
        stripped[j] = 0;
        k = atol(stripped);
        has_k = 1;
        kexpr = sfmt("%ld", k);
    } else {
        has_k = 0;
        kexpr = as_int(dim);
    }
    if (sym->is_param) {
        const char *nm = bname(sym->name);

        if (has_k && k == 0)
            return sfmt("%d", cv.opt_base);
        return sfmt("(%s)[%s]", nm, kexpr);
    }
    if (!has_k)
        cv_err("BOUND() on a DIMmed array needs a constant dimension");
    if (k == 0)
        return sfmt("%d", cv.opt_base);
    if (k > sym->ndims)
        return "0";
    {
        /* Python sym.dims[k - 1]: mirror negative indexing too */
        long idx = k - 1;

        if (idx < 0)
            idx += sym->ndims;
        if (idx < 0)
            mm_error("line %d: BOUND() dimension out of range", cv.lineno);
        return sfmt("((%s) - 1)", sym->dims[idx]);
    }
}

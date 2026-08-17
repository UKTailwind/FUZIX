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
    v.stype = NULL;
    v.tm = 0;
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

/* mmb2c.py's varaddr.  PEEK(VARADDR v) - the address of a variable's
   storage.

   MMBasic's findvar with V_EMPTY_OK | V_NOFIND_ERR: the variable must
   already exist, and a whole array written a() is legal and answers
   with element 0.

   The forms and what each is the address OF:

       v           a scalar's own storage
       s$          the LENGTH BYTE, where an MMBasic string starts
       a(i)        that element
       a()         element 0, whatever the rank

   A string and an array are already addresses in C - a char[] and an
   array both decay - so only the scalars take an '&'.  Cast through
   uintptr_t for the same reason mmb_peek.h does: the board is 32-bit
   and the gates are 64-bit, and this is the cast right on both. */
const char *varaddr(void)
{
    struct tok *t = nxt();
    char *canon;
    int sfx;
    struct sym *s;

    if (t->kind != T_ID)
        cv_err("PEEK(VARADDR ...) needs a variable");
    canon = split_suffix(t->text, &sfx);
    s = sym_lookup(canon);
    if (s == NULL)
        cv_err("'%s' has not been declared", canon);
    if (sfx != TY_NONE && sfx != s->ty)
        cv_err("'%s' is %s but used as %s", canon, tyname_of(s->ty),
               tyname_of(sfx));
    if (s->is_const)
        cv_err("'%s' is a CONST, so it has no address", canon);
    if (is_op("(", 0) && is_op(")", 1)) {
        struct flat f;
        cv.i += 2;
        if (!s->is_array)
            cv_err("'%s' is not an array", canon);
        f = array_flat(s);
        return sfmt("(MMINTEGER)(uintptr_t)(%s)", f.ptr);
    }
    if (is_op("(", 0)) {
        const char *el;
        if (!s->is_array)
            cv_err("'%s' is not an array", canon);
        el = index_of(s);
        if (s->ty == TY_S)
            return sfmt("(MMINTEGER)(uintptr_t)(%s)", el);
        return sfmt("(MMINTEGER)(uintptr_t)&(%s)", el);
    }
    if (s->is_array)
        cv_err("array '%s' needs () or an index", canon);
    if (s->ty == TY_S)
        return sfmt("(MMINTEGER)(uintptr_t)(%s)", s->acc);
    return sfmt("(MMINTEGER)(uintptr_t)&(%s)", s->acc);
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
            /* a C comparison is already the 1 or 0 MMBasic defines;
               the old "? 1 : 0" was a branch diamond the compiler
               never folded, paid on every comparison */
            v = mkval(sfmt("((%s) %s (%s))", v.code, cop, r.code),
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
            return mkval(sfmt("((%s) == 0)", as_flt(v)), TY_I);
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
                /* op_div checks the divisor first where a bare C '/'
                   answers inf.  The check exists so ON ERROR can trap
                   the error; a program with no ON ERROR has nothing to
                   trap it with - a divide by zero there is a bug the
                   program needs fixing either way - so only trapping
                   programs pay the runtime call.  A literal divisor
                   that is not zero needs no check in either world:
                   dividing by 180, 86400 or pi is most of the division
                   a real program does. */
                const char *rd = as_flt(r);

                if (nonzero_literal(rd) || !checks_on())
                    v = mkval(sfmt("((%s) / (%s))", as_flt(v), rd), TY_F);
                else
                    v = mkval(sfmt("mm_fdiv(%s, %s)", as_flt(v), rd),
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
        /* the Python passes v's type through whole (it can be the
         * struct tuple), so mutate the code and keep the rest */
        v.code = sfmt("(-(%s))", v.code);
        return v;
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
        /* type carried through whole (it can be the struct tuple) */
        v.code = sfmt("(%s)", v.code);
        return v;
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
    struct shead sh;
    int as_array;
    struct sym *s;

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

    /* Call(name$ [, args...]) - the function form of CALL.  A user
     * routine named CALL wins above, exactly as it would for a
     * built-in.  A literal name resolves here and now to a direct
     * call; only a run-time name needs the dispatcher. */
    if (strcmp(up, "CALL") == 0 && is_op("(", 0)) {
        struct tok *w2;
        struct arglist args;
        struct val v;
        struct calldisp *d;

        cv.i++;
        w2 = peek(0);
        if (w2 != NULL && w2->kind == T_STR) {
            int sfx2;
            char *canon2 = split_suffix(lower(w2->text), &sfx2);

            r = routine_get(canon2);
            if (r == NULL || !r->is_func)
                cv_err("CALL: no FUNCTION named '%s'", w2->text);
            if (sfx2 != TY_NONE && r->ty != sfx2)
                cv_err("'%s' is %s but named as %s", canon2,
                       tyname_of(r->ty), tyname_of(sfx2));
            cv.i++;
            args.n = 0;
            while (accept_op(","))
                arg_item(&args.a[args.n++]);
            expect_op(")");
            return emit_call(r, &args);
        }
        v = expr();
        if (v.ty != TY_S)
            cv_err("CALL needs the routine name in a string");
        args.n = 0;
        while (accept_op(","))
            arg_item(&args.a[args.n++]);
        expect_op(")");
        d = call_dispatch(1, &args);
        return emit_call_byname(d, v.code, &args);
    }

    if (strcmp(up, "STRUCT") == 0 && is_op("(", 0))
        return struct_fn();

    b = builtin_get(up);
    if (b != NULL && (b->minargs == 0 || is_op("(", 0)))
        return call_builtin(up);

    if (struct_head(word, &sh)) {
        const char *base = struct_base(sh.s);
        return member_value(member_path(base, sh.s->stype,
                                        sh.parts, sh.nparts, sh.sfx));
    }

    as_array = is_op("(", 0);
    if (!as_array) {
        int gp = gp_pin(word, sym_lookup(canon));
        if (gp >= 0)
            return mkval(sfmt("%dLL", gp), TY_I);
    }
    s = reference(word, as_array);
    if (s->stype != NULL) {
        const char *base;
        if (as_array && !s->is_array)
            cv_err("'%s' is not an array", canon);
        if (s->is_array && !as_array)
            cv_err("struct array '%s' used without an index", canon);
        base = as_array ? index_of(s) : s->acc;
        return member_value(member_path(base, s->stype, NULL, 0, sfx));
    }
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
    if (s->is_param || s->dynamic) {
        /* MMBasic gives an array parameter no rank of its own - it
         * inherits whatever was passed - so the subscripts are folded
         * into one offset using the bounds handed in alongside it.
         * An array DIMmed with a run-time bound is the same shape and
         * folds the same way, out of its own bounds table. */
        const char *b = s->dynamic ? s->bacc : bname(s->name);
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

        /* INSIDE A FUNCTION, ITS OWN NAME IS A VARIABLE - the return
           value - and MMBasic passes it by reference like any other.
           The test below skips every name that is a routine, so this
           one fell through to the expression path and went BY VALUE
           through a temporary: the callee wrote into the temporary and
           the function returned whatever it had before.

           It compiles and runs, which is what makes it bad.  Found in
           Pico-Vaders, whose whole controller layer is
               Function twait%(...)
                 Call ctrl$, twait%
           so every button read came back zero. */
        if (cv.cur != NULL && cv.cur->is_func
            && strcmp(canon, cv.cur->name) == 0) {
            struct tok *nx = peek(1);
            if (nx == NULL
                || (nx->kind == T_OP
                    && (strcmp(nx->text, ",") == 0
                        || strcmp(nx->text, ")") == 0
                        || strcmp(nx->text, ":") == 0))) {
                struct sym *s;
                if (sfx != TY_NONE && sfx != cv.cur->ty)
                    cv_err("'%s' is %s but used as %s", canon,
                           tyname_of(cv.cur->ty), tyname_of(sfx));
                s = sym_new(canon, cv.cur->ty, retacc());
                cv.i++;
                out->kind = ARG_VAR;
                out->s = s;
                return;
            }
        }
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
            /* one ELEMENT of an array: a(i), a(i,j)
             *
             * An element is a variable like any other and MMBasic passes
             * it by reference - findvar() on "x(k)" hands the sub a
             * pointer to that element (MMBasic.c:2230, "set argvalue to
             * point to the variable's data").  Treated as an expression
             * it was copied into a temporary, so a sub that writes to
             * its parameter wrote into the temporary and the caller's
             * array never changed.
             *
             * brownian.bas is what found it: its whole animation is
             * "vector i, direction(i), 1, x(i), y(i)" updating x() and
             * y() through the parameters.  Every atom was drawn at its
             * starting position for ever. */
            if (nxt1 != NULL && nxt1->kind == T_OP
                && strcmp(nxt1->text, "(") == 0) {
                struct sym *s = sym_lookup(canon);

                if (s != NULL && s->is_array && !s->is_const) {
                    /* Only when the ')' ENDS the argument: a(i)+1 is an
                       expression and must stay one.  Scanned, not
                       parsed-and-backtracked, so nothing is consumed
                       unless this really is a bare element. */
                    int k = 1, depth = 0;
                    struct tok *tk = NULL, *after;

                    for (;;) {
                        tk = peek(k);
                        if (tk == NULL)
                            break;
                        if (tk->kind == T_OP && strcmp(tk->text, "(") == 0)
                            depth++;
                        else if (tk->kind == T_OP
                                 && strcmp(tk->text, ")") == 0) {
                            depth--;
                            if (depth == 0)
                                break;
                        }
                        k++;
                    }
                    after = (tk != NULL) ? peek(k + 1) : NULL;
                    if (tk != NULL
                        && (after == NULL
                            || (after->kind == T_OP
                                && (strcmp(after->text, ",") == 0
                                    || strcmp(after->text, ")") == 0
                                    || strcmp(after->text, ":") == 0)))) {
                        cv.i += 1;
                        out->kind = ARG_ELEM;
                        out->s = s;
                        out->v = mkval(index_of(s), s->ty);
                        return;
                    }
                }
            }
        }
    }
    out->kind = ARG_VAL;
    out->v = expr();
}

/* -- CALL by name: execute a SUB or FUNCTION named in a string --------
 *
 * MMBasic resolves the name at run time (cmd_call / the Call()
 * function).  Compiled, the possible targets are known: every routine
 * whose parameter list matches the shape of the arguments at this CALL
 * site.  One dispatcher is emitted per distinct shape (__mm_calld_N,
 * after the routine bodies): it compares the name - case-insensitively,
 * with and without the type suffix - against each candidate and
 * forwards the arguments.  A name that matches nothing is a run-time
 * error, as in the interpreter.  Documented divergence: MMBasic would
 * also find a routine whose parameters do NOT fit these arguments and
 * fail inside it; here such a routine is simply never a candidate. */
/* Canonical names that appear as string literals anywhere in the
 * program - the set a run-time CALL name can plausibly draw from when
 * the site's own shape is ambiguous.  Static token buffer: MAXTOKS
 * tokens do not fit the board build's stack. */
static struct tok lit_toks[MAXTOKS];

static void lit_names_build(void)
{
    int idx, k;

    if (cv.lit_names_built)
        return;
    cv.lit_names_built = 1;
    for (idx = 0; idx < src_nlines; idx++) {
        jmp_buf jb, *saved = err_jmp;
        int n;

        err_jmp = &jb;
        if (setjmp(jb) != 0) {
            err_jmp = saved;
            continue;
        }
        n = tokenize(src_lines[idx], idx + 1, lit_toks);
        err_jmp = saved;
        for (k = 0; k < n; k++) {
            int sfx, j, have = 0;
            const char *nm;

            if (lit_toks[k].kind != T_STR)
                continue;
            nm = split_suffix(lower(lit_toks[k].text), &sfx);
            for (j = 0; j < cv.nlit_names; j++)
                if (strcmp(cv.lit_names[j], nm) == 0)
                    have = 1;
            if (!have) {
                GROW(cv.lit_names, cv.nlit_names, cv.clit_names);
                cv.lit_names[cv.nlit_names++] = pstr(nm);
            }
        }
    }
}

static int lit_name_has(const char *nm)
{
    int j;

    for (j = 0; j < cv.nlit_names; j++)
        if (strcmp(cv.lit_names[j], nm) == 0)
            return 1;
    return 0;
}

static int call_arg_ty(struct arg *a)
{
    if (a == NULL || a->kind == ARG_NONE)
        cv_err("CALL cannot omit an argument");
    if (a->kind == ARG_VAR || a->kind == ARG_ELEM)
        return a->s->ty;
    if (a->kind == ARG_VAL)
        return a->v.ty;
    cv_err("a whole array cannot be passed through CALL yet");
    return 0;
}

struct calldisp *call_dispatch(int is_func, struct arglist *args)
{
    int tys[MAXARGS];
    struct routine *exact[MAXCALLC], *coerced[MAXCALLC];
    int nexact = 0, ncoerced = 0;
    struct routine **cands;
    int ncands, k, j, rty;
    struct calldisp *d;

    for (k = 0; k < args->n; k++)
        tys[k] = call_arg_ty(&args->a[k]);
    for (j = 0; j < cv.nroutines; j++) {
        struct routine *r = cv.routines[j];
        int ok_exact = 1, ok_coerce = 1, skip = 0;

        if ((r->is_func != 0) != (is_func != 0))
            continue;
        /* trailing arguments may be omitted, exactly as they may in a
           direct call: the candidate's spare parameters take their
           defaults in the dispatcher body */
        if (r->nparams < args->n)
            continue;
        for (k = 0; k < r->nparams; k++)
            if (r->params[k]->stype != NULL || r->params[k]->is_array)
                skip = 1;
        if (skip)
            continue;
        for (k = 0; k < args->n; k++) {
            struct sym *p = r->params[k];

            if (p->ty == tys[k])
                continue;
            ok_exact = 0;
            if (args->a[k].kind == ARG_VAL && tys[k] == TY_I
                && p->ty == TY_F)
                continue;
            ok_coerce = 0;
            break;
        }
        if (ok_exact) {
            if (nexact < MAXCALLC)
                exact[nexact++] = r;
        } else if (ok_coerce) {
            if (ncoerced < MAXCALLC)
                coerced[ncoerced++] = r;
        }
    }
    cands = nexact ? exact : coerced;
    ncands = nexact ? nexact : ncoerced;
    if (ncands == 0)
        cv_err("CALL: no SUB or FUNCTION takes arguments of "
               "this shape");
    rty = cands[0]->ty;
    if (is_func) {
        int mixed = 0;

        for (j = 0; j < ncands; j++)
            if (cands[j]->ty != rty)
                mixed = 1;
        if (mixed) {
            /* An ambiguous shape (a zero-argument Call() matches every
             * zero-argument function).  Narrow to the routines the
             * program actually NAMES in a string literal somewhere -
             * the set a run-time name can plausibly draw from. */
            struct routine *nar[MAXCALLC];
            int nnar = 0;

            lit_names_build();
            for (j = 0; j < ncands; j++)
                if (lit_name_has(cands[j]->name))
                    nar[nnar++] = cands[j];
            if (nnar) {
                for (j = 0; j < nnar; j++)
                    cands[j] = nar[j];
                ncands = nnar;
                rty = cands[0]->ty;
            }
        }
    }
    for (j = 0; j < ncands; j++)
        if (is_func && cands[j]->ty != rty)
            cv_err("CALL: functions matching these arguments "
                   "return different types ('%s' and '%s'); "
                   "name the target with a literal string, or "
                   "make their types uniform",
                   cands[0]->name, cands[j]->name);
    {
        /* candidates whose by-reference pattern differs from the
           representative's cannot share its formals */
        struct routine *base = cands[0];
        int nkeep = 0;

        for (j = 0; j < ncands; j++) {
            for (k = 0; k < args->n; k++)
                if ((cands[j]->params[k]->byref != 0)
                    != (base->params[k]->byref != 0))
                    break;
            if (k == args->n)
                cands[nkeep++] = cands[j];
        }
        ncands = nkeep;
    }
    for (j = 0; j < cv.ncalld; j++) {
        int m;

        d = &cv.calld[j];
        if (d->is_func != is_func)
            continue;
        if (is_func && d->rty != rty)
            continue;
        if (d->nparams != args->n || d->ncands != ncands)
            continue;
        for (k = 0; k < d->nparams; k++)
            if (d->pty[k] != cands[0]->params[k]->ty
                || d->pbyref[k] != (cands[0]->params[k]->byref != 0))
                break;
        if (k < d->nparams)
            continue;
        for (m = 0; m < ncands; m++)
            if (d->cands[m] != cands[m])
                break;
        if (m == ncands)
            return d;
    }
    GROW(cv.calld, cv.ncalld, cv.ccalld);
    d = &cv.calld[cv.ncalld];
    d->is_func = is_func;
    d->rty = is_func ? rty : TY_NONE;
    d->nparams = args->n;
    for (k = 0; k < args->n; k++) {
        d->pty[k] = cands[0]->params[k]->ty;
        d->pbyref[k] = (cands[0]->params[k]->byref != 0);
    }
    d->name = pstr(sfmt("__mm_calld_%d", cv.ncalld));
    d->rep = cands[0];
    d->ncands = ncands;
    for (j = 0; j < ncands; j++)
        d->cands[j] = cands[j];
    cv.ncalld++;
    return d;
}

struct val emit_call_byname(struct calldisp *d, const char *nmexpr,
                            struct arglist *args)
{
    struct routine *rep = d->rep;
    const char *joined = NULL;
    int k;

    if (d->is_func && rep->ty == TY_S) {
        cv.tmp_used = 1;
        joined = "mm_tmp()";
    }
    joined = joined ? sfmt("%s, %s", joined, nmexpr) : nmexpr;
    for (k = 0; k < d->nparams; k++) {
        const char *piece = pass_arg(rep->params[k], &args->a[k], rep);

        joined = sfmt("%s, %s", joined, piece);
    }
    return mkval(sfmt("%s(%s)", d->name, joined),
                 d->is_func ? rep->ty : TY_NONE);
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
    if (p->stype != NULL) {
        /* always by reference, exactly as the firmware passes them */
        if (a == NULL)
            cv_err("a structure argument to '%s' cannot be "
                   "omitted", r->name);
        if (a->kind == ARG_VAR) {
            if (a->s->stype == NULL
                || strcmp(a->s->stype, p->stype) != 0)
                cv_err("structure type mismatch in call to '%s'",
                       r->name);
            return sfmt("&%s", a->s->acc);
        }
        if (a->kind == ARG_ELEM) {
            /* An element of an array OF structures - structtest's TEST
               5.  It reaches here as its own kind now, and the address
               of the element is what the callee wants, the same as
               every other structure argument. */
            if (a->s->stype == NULL
                || strcmp(a->s->stype, p->stype) != 0)
                cv_err("structure type mismatch in call to '%s'",
                       r->name);
            return sfmt("&%s", a->v.code);
        }
        if (a->kind == ARG_VAL) {
            struct val v = a->v;
            if (v.ty != TY_T || strcmp(v.stype, p->stype) != 0)
                cv_err("structure type mismatch in call to '%s'",
                       r->name);
            return sfmt("&%s", v.code);
        }
        cv_err("'%s' expects a structure here", r->name);
    }
    if (a != NULL && a->kind == ARG_VAR && a->s->stype != NULL)
        cv_err("a structure cannot be passed to a plain "
               "parameter of '%s'", r->name);
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
        if (s->is_param || s->dynamic) {
            bnd = s->dynamic ? s->bacc : bname(s->name);
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
        if (a->kind == ARG_ELEM) {
            /* A string array element is already a char[]; by reference
               it IS the element, by value a scratch copy. */
            if (a->s->ty != TY_S)
                cv_err("type mismatch in call to '%s'", r->name);
            if (p->byref)
                return a->v.code;
            cv.tmp_used = 1;
            return sfmt("mm_scopy(%s)", a->v.code);
        }
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
        } else if (a->kind == ARG_ELEM) {
            /* The address of the element itself, so the sub writes into
               the caller's array - the whole point of a by-reference
               parameter.  A type that does not match falls through to a
               converted copy, exactly as a scalar of the wrong type
               does. */
            if (a->s->ty == p->ty && p->byref)
                return sfmt("&%s", a->v.code);
            val = (p->ty == TY_I) ? as_int(a->v) : as_flt(a->v);
        } else {
            val = (p->ty == TY_I) ? as_int(a->v) : as_flt(a->v);
        }
        if (p->byref) {
            if (cv.fcc) {
                /* No compound literals in FCC: the runtime parks the
                 * value in a small ring of scratch slots and returns
                 * its address.  The slot is scratch wound back by
                 * mm_release, so it must count as a consumed
                 * temporary - the per-iteration loop releases used
                 * to mask this, and removing them overflowed the
                 * byref stack on the eclipse. */
                cv.tmp_used = 1;
                return sfmt("mm_byref_%s(%s)",
                            (p->ty == TY_I) ? "i" : "f", val);
            }
            /* A compound literal needs no scratch slot, but the release
             * this asks for is still wanted: whatever temporaries the
             * PREVIOUS statement left behind would otherwise be held
             * for the whole of the call, and in a recursive routine
             * that is every level at once.  Nine levels was the wall;
             * the --fcc path above never had it because the by-ref slot
             * made the statement ask for a release anyway. */
            cv.tmp_used = 1;
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

/* Does a whole array - written a() - start here?
 *
 * MMBasic decides PIXEL's two forms at run time, by asking whether the
 * argument it was handed is an array (getargaddress reports a count).
 * Here it has to be a question about the text, because the two forms
 * compile to different calls; a() is the spelling MMBasic's own
 * documentation uses for a whole array. */
int is_array_arg(void)
{
    struct tok *t = peek(0);

    return t != NULL && t->kind == T_ID
           && is_op("(", 1) && is_op(")", 2);
}

/* (pointer to element 0, element count) for a whole array. */
struct flat array_flat(struct sym *s)
{
    struct flat r;

    if (!s->is_array)
        cv_err("'%s' is not an array", s->name);
    if (s->is_param || s->dynamic) {
        r.ptr = s->acc;
        r.cnt = sfmt("mm_arr_count(%s)",
                     s->dynamic ? s->bacc : bname(s->name));
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
    if (sym->is_param || sym->dynamic) {
        const char *nm = sym->dynamic ? sym->bacc : bname(sym->name);

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

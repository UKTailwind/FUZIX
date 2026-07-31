/* mmbc_decl.c - the declaration machinery: pass_routine_names,
 * pass_declarations, labels, SUB/FUNCTION headers, DIM/LOCAL/STATIC/
 * CONST, OPTION and DATA collection.
 *
 * Mirrors mmb2c.py pass_routine_names (1402) through do_const (1820).
 * do_declare/emit_initialiser/do_const run in EVERY pass: expression
 * parsing must advance the token stream even when nothing is emitted. */

#include "mmbc.h"

int type_word(void);

struct routine *routine_get(const char *canon)
{
    int k;
    for (k = 0; k < cv.nroutines; k++)
        if (strcmp(cv.routines[k]->name, canon) == 0)
            return cv.routines[k];
    return NULL;
}

static struct sym *globals_get(const char *canon)
{
    int k;
    for (k = 0; k < cv.nglobals; k++)
        if (strcmp(cv.globals[k]->name, canon) == 0)
            return cv.globals[k];
    return NULL;
}

static int routine_name_known(const char *canon)
{
    int k;
    for (k = 0; k < cv.nroutine_names; k++)
        if (strcmp(cv.routine_names[k], canon) == 0)
            return 1;
    return 0;
}

static char *pfx_dunder(const char *pfx, const char *name)
{
    /* pfx + name.replace('.', '__') */
    size_t n = strlen(name);
    char *out = salloc(strlen(pfx) + n * 2 + 1);
    size_t j = strlen(pfx);
    size_t i;

    memcpy(out, pfx, j);
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

/* Cheap pre-scan so that label detection and forward calls know every
 * SUB/FUNCTION name before anything else runs. */
void pass_routine_names(void)
{
    static struct tok toks[MAXTOKS];
    volatile int idx;           /* survives the setjmp per iteration */
    int nt, k, sfx;

    for (idx = 0; idx < src_nlines; idx++) {
        jmp_buf jb, *saved = err_jmp;
        cv.lineno = idx + 1;
        err_jmp = &jb;
        if (setjmp(jb) != 0) {
            err_jmp = saved;
            continue;
        }
        nt = tokenize(src_lines[idx], cv.lineno, toks);
        err_jmp = saved;
        k = 0;
        if (k < nt && toks[k].kind == T_NUM)
            k++;
        if (k + 1 < nt && toks[k].kind == T_ID
            && toks[k + 1].kind == T_OP
            && strcmp(toks[k + 1].text, ":") == 0)
            k += 2;
        if (k + 1 < nt && toks[k].kind == T_ID
            && (strcmp(toks[k].up, "SUB") == 0
                || strcmp(toks[k].up, "FUNCTION") == 0)
            && toks[k + 1].kind == T_ID) {
            char *canon = split_suffix(toks[k + 1].text, &sfx);
            if (!routine_name_known(canon)) {
                GROW(cv.routine_names, cv.nroutine_names,
                     cv.croutine_names);
                cv.routine_names[cv.nroutine_names++] = pstr(canon);
            }
        }
    }
}

void pass_declarations(void)
{
    volatile int idx;

    cv.mode = M_DECL;
    cv.cur = NULL;
    for (idx = 0; idx < src_nlines; idx++) {
        jmp_buf jb, *saved = err_jmp;
        cv.lineno = idx + 1;
        err_jmp = &jb;
        if (setjmp(jb) != 0) {
            err_jmp = saved;
            errors_add(err_msg);
            continue;
        }
        cv.ntoks = tokenize(src_lines[idx], cv.lineno, cv.toks);
        err_jmp = saved;
        cv.i = 0;
        strip_line_number();
        while (!at_end()) {
            jmp_buf jb2, *saved2;
            if (accept_op(":"))
                continue;
            saved2 = err_jmp;
            err_jmp = &jb2;
            if (setjmp(jb2) == 0) {
                decl_statement();
                err_jmp = saved2;
            } else {
                err_jmp = saved2;
                skip_statement();
            }
        }
    }
    cv.cur = NULL;
}

void place_label(const char *canon)
{
    struct label *l;
    int depth, k;

    if (cv.mode == M_DECL) {
        l = label_rec(canon);
        l->placed = 1;
        l->lineno = cv.lineno;
        l->has_data_at = 1;
        l->data_at = cv.ndata;
        l->routine = cv.cur ? cv.cur->name : "";
        return;
    }
    depth = 0;
    for (k = 0; k < cv.nblocks; k++)
        if (strcmp(cv.blocks[k].kind, "routine") != 0)
            depth++;
    if (cv.mode == M_SCAN) {
        l = label_rec(canon);
        l->has_depth = 1;
        l->depth = depth;
        return;
    }
    l = label_rec(canon);
    if (!l->used)
        return;             /* nothing jumps here, so C needs no label */
    /* only worth a warning when something jumps in from further out */
    if (depth > 0 && (l->has_goto ? l->goto_depth : depth) < depth) {
        for (k = 0; k < cv.nblocks; k++) {
            if (strcmp(cv.blocks[k].kind, "routine") != 0) {
                char *kind = upper(cv.blocks[k].kind);
                cv_warn("label '%s' sits inside %s %s block but is "
                        "jumped to from outside it; the block's set-up "
                        "will be skipped",
                        canon,
                        strchr("AEIOU", kind[0]) ? "an" : "a", kind);
                break;
            }
        }
    }
    raw(sfmt("%s: ;", clabel(canon)));
}

/* A leading integer is a line number and a leading 'name:' is a label;
 * both are GOTO targets.  A bare subroutine call followed by a colon
 * (Counter : Counter) must not be mistaken for a label, which is why
 * the routine names are collected first. */
void strip_line_number(void)
{
    struct tok *t;
    int sfx;

    t = peek(0);
    if (t != NULL && t->kind == T_NUM && strcmp(t->up, "I") == 0) {
        cv.i++;
        place_label(t->text);
    }
    t = peek(0);
    if (t != NULL && t->kind == T_ID && is_op(":", 1)
        && !kw_in(t->up) && builtin_get(t->up) == NULL) {
        char *canon = split_suffix(t->text, &sfx);
        if (routine_get(canon) == NULL && !routine_name_known(canon)) {
            cv.i += 2;
            place_label(canon);
        }
    }
}

void skip_statement(void)
{
    while (!at_end() && !is_op(":", 0))
        cv.i++;
}

void decl_statement(void)
{
    struct tok *t = peek(0);
    const char *up;

    if (t == NULL)
        return;
    up = (t->kind == T_ID) ? t->up : t->text;

    if (strcmp(up, "OPTION") == 0) {
        cv.i++;
        do_option();
        return;
    }
    if (strcmp(up, "SUB") == 0 || strcmp(up, "FUNCTION") == 0) {
        cv.i++;
        decl_routine(strcmp(up, "FUNCTION") == 0);
        skip_statement();
        return;
    }
    if (strcmp(up, "END") == 0
        && (is_kw("SUB", 1) || is_kw("FUNCTION", 1))) {
        cv.cur = NULL;
        cv.i += 2;
        return;
    }
    if (strcmp(up, "DIM") == 0 || strcmp(up, "LOCAL") == 0
        || strcmp(up, "STATIC") == 0 || strcmp(up, "CONST") == 0) {
        cv.i++;
        do_declare(up);
        return;
    }
    if (strcmp(up, "DATA") == 0) {
        cv.i++;
        collect_data();
        return;
    }
    skip_statement();
}

static void data_add(int kind, const char *f, const char *i,
                     const char *sv)
{
    struct data_item *d;
    GROW(cv.data, cv.ndata, cv.cdata);
    d = &cv.data[cv.ndata++];
    d->kind = kind;
    d->f = pstr(f);
    d->i = pstr(i);
    d->sv = pstr(sv);
}

/* DATA items are gathered once, in the declaration pass, so RESTORE
 * <label> can be resolved to an index and the whole table emitted as
 * static C.  MMBasic keeps the raw text and converts on READ, so each
 * entry carries both forms. */
void collect_data(void)
{
    for (;;) {
        int start = cv.i;
        struct tok *t = peek(0);
        struct tok *nxt1 = peek(1);
        int ends = (nxt1 == NULL
                    || (nxt1->kind == T_OP
                        && (strcmp(nxt1->text, ",") == 0
                            || strcmp(nxt1->text, ":") == 0)));
        if (t != NULL && t->kind == T_STR && ends) {
            cv.i++;
            data_add(1, "0.0", "0LL", c_string_literal(t->text));
        } else if (t != NULL && t->kind == T_ID && ends
                   && !kw_in(t->up)) {
            cv.i++;
            data_add(1, "0.0", "0LL", c_string_literal(t->text));
        } else {
            struct val v = expr();
            char *text = source_text(start, cv.i);
            if (v.ty == TY_S)
                data_add(1, "0.0", "0LL", v.code);
            else if (v.ty == TY_I)
                data_add(0, "0.0", v.code, c_string_literal(text));
            else
                data_add(2, v.code, "0LL", c_string_literal(text));
        }
        if (!accept_op(","))
            break;
    }
}

/* Rebuild the source of tokens [a, b) - the text form of a numeric
 * DATA item, for when it is READ into a string. */
char *source_text(int a, int b)
{
    size_t need = 1;
    char *out;
    size_t j = 0;
    int k;

    for (k = a; k < b; k++)
        need += strlen(cv.toks[k].text) + 3;
    out = salloc(need);
    for (k = a; k < b; k++) {
        if (k > a)
            out[j++] = ' ';
        if (cv.toks[k].kind == T_STR) {
            out[j++] = '"';
            j += (size_t)sprintf(out + j, "%s", cv.toks[k].text);
            out[j++] = '"';
        } else {
            j += (size_t)sprintf(out + j, "%s", cv.toks[k].text);
        }
    }
    out[j] = 0;
    return out;
}

static struct routine *routine_new(const char *canon, int is_func)
{
    struct routine *r = palloc(sizeof(*r));

    memset(r, 0, sizeof(*r));
    r->name = pstr(canon);
    r->disp = r->name;
    r->cname = pstr(pfx_dunder("f_", canon));
    r->is_func = is_func;
    r->ty = TY_F;
    return r;
}

void decl_routine(int is_func)
{
    struct tok *t = nxt();
    char *canon;
    int sfx;
    struct routine *r;

    if (t->kind != T_ID)
        cv_err("SUB/FUNCTION needs a name");
    canon = split_suffix(t->text, &sfx);
    if (routine_get(canon) != NULL)
        cv_err("'%s' defined twice", canon);
    r = routine_new(canon, is_func);
    r->disp = pstr(t->text);
    r->line = cv.lineno;
    r->ty = (sfx != TY_NONE) ? sfx : cv.opt_default;
    GROW(cv.routines, cv.nroutines, cv.croutines);
    cv.routines[cv.nroutines++] = r;
    cv.cur = r;
    /* parameter list */
    if (accept_op("(")) {
        if (!accept_op(")")) {
            for (;;) {
                decl_param(r);
                if (!accept_op(","))
                    break;
            }
            expect_op(")");
        }
    } else if (!stmt_end() && !is_kw("AS", 0)) {
        for (;;) {
            decl_param(r);
            if (!accept_op(","))
                break;
        }
    }
    /* trailing  AS <type>  for functions */
    if (accept_kw("AS")) {
        int ty = type_word();
        if (sfx != TY_NONE && sfx != ty)
            cv_err("return type conflicts with the name suffix");
        r->ty = ty;
    }
}

void decl_param(struct routine *r)
{
    int byref = 1;
    struct tok *t;
    char *canon;
    int sfx, ty, nd = 0;
    int has_dims = 0;
    struct sym *s;

    if (accept_kw("BYVAL"))
        byref = 0;
    else if (accept_kw("BYREF"))
        byref = 1;
    t = nxt();
    if (t->kind != T_ID)
        cv_err("bad parameter");
    canon = split_suffix(t->text, &sfx);
    if (accept_op("(")) {
        has_dims = 1;
        nd = 1;
        while (accept_op(","))
            nd++;
        expect_op(")");
        /* a rank hint only: the real rank comes from the array the
         * caller passes */
    }
    ty = sfx;
    if (accept_kw("AS")) {
        int ty2 = type_word();
        if (ty != TY_NONE && ty != ty2)
            cv_err("parameter type conflict");
        ty = ty2;
    }
    if (ty == TY_NONE)
        ty = cv.opt_default;
    s = palloc(sizeof(*s));
    memset(s, 0, sizeof(*s));
    s->name = pstr(canon);
    s->disp = s->name;
    s->ty = ty;
    s->acc = "";
    s->is_param = 1;
    s->byref = byref;
    s->where = cv.lineno;
    s->declared_in = r->name;
    if (has_dims) {
        int k;
        s->is_array = 1;
        s->ndims = nd;
        s->dims = palloc(sizeof(char *) * (size_t)nd);
        for (k = 0; k < nd; k++)
            s->dims[k] = "0";
        s->acc = pstr(pfx_dunder("p_", canon));
    } else if (ty == TY_S) {
        s->acc = pstr(pfx_dunder("p_", canon));
    } else if (byref) {
        s->acc = pstr(sfmt("(*%s)", pfx_dunder("p_", canon)));
    } else {
        s->acc = pstr(pfx_dunder("p_", canon));
    }
    GROW(r->params, r->nparams, r->cparams);
    r->params[r->nparams++] = s;
    GROW(r->locals, r->nlocals, r->clocals);
    r->locals[r->nlocals++] = s;
    GROW(r->local_order, r->nlocal_order, r->clocal_order);
    r->local_order[r->nlocal_order++] = s->name;
}

int type_word(void)
{
    struct tok *t = nxt();

    if (t->kind != T_ID)
        cv_err("type expected");
    if (strcmp(t->up, "INTEGER") == 0)
        return TY_I;
    if (strcmp(t->up, "FLOAT") == 0)
        return TY_F;
    if (strcmp(t->up, "STRING") == 0)
        return TY_S;
    cv_err("unknown type '%s'", t->text);
    return 0;
}

void do_option(void)
{
    struct tok *t = peek(0);
    struct tok *w;

    if (t == NULL)
        return;
    if (strcmp(t->up, "DEFAULT") == 0) {
        cv.i++;
        w = nxt();
        if (strcmp(w->up, "INTEGER") == 0)
            cv.opt_default = TY_I;
        else if (strcmp(w->up, "FLOAT") == 0)
            cv.opt_default = TY_F;
        else if (strcmp(w->up, "STRING") == 0)
            cv.opt_default = TY_S;
        else if (strcmp(w->up, "NONE") == 0)
            cv.opt_default = TY_NONE;
        return;
    }
    if (strcmp(t->up, "EXPLICIT") == 0) {
        cv.i++;
        cv.opt_explicit = 1;
        if (is_kw("OFF", 0)) {
            cv.opt_explicit = 0;
            cv.i++;
        }
        return;
    }
    if (strcmp(t->up, "BASE") == 0) {
        cv.i++;
        w = nxt();
        cv.opt_base = atoi(w->text);
        return;
    }
    skip_statement();
}

static void emit_initialiser(struct sym *s, int is_static);
static char *linear_index(struct sym *s, int k);

/* Runs in every pass.  In the 'decl' pass it records the symbols; in
 * the 'emit' pass it produces the initialisation code (the
 * declarations themselves are hoisted to the top of the C scope). */
void do_declare(const char *kw)
{
    const char *scope;
    int is_static;
    int group_ty = TY_NONE;
    struct tok *t;

    if (strcmp(kw, "CONST") == 0) {
        do_const();
        return;
    }
    scope = (strcmp(kw, "DIM") == 0) ? "global" : "local";
    if (strcmp(scope, "local") == 0 && cv.cur == NULL)
        cv_err("%s is only valid inside a SUB or FUNCTION", kw);
    is_static = (strcmp(kw, "STATIC") == 0);

    /* optional leading type applying to the whole list */
    t = peek(0);
    if (t != NULL && t->kind == T_ID
        && (strcmp(t->up, "INTEGER") == 0 || strcmp(t->up, "FLOAT") == 0
            || strcmp(t->up, "STRING") == 0))
        group_ty = type_word();

    for (;;) {
        char *canon;
        int sfx, ty;
        const char **dims = NULL;
        int ndims = 0, cdims = 0;
        struct sym *s;

        t = nxt();
        if (t->kind != T_ID)
            cv_err("variable name expected in %s", kw);
        canon = split_suffix(t->text, &sfx);
        if (accept_op("(")) {
            for (;;) {
                struct val v = expr();
                GROW(dims, ndims, cdims);
                dims[ndims++] = sfmt("(%s) + 1", as_int(v));
                if (!accept_op(","))
                    break;
            }
            expect_op(")");
        }
        ty = (sfx != TY_NONE) ? sfx : group_ty;
        if (accept_kw("AS")) {
            int ty2 = type_word();
            if (ty != TY_NONE && ty != ty2)
                cv_err("conflicting types for '%s'", canon);
            ty = ty2;
        }
        if (ty == TY_NONE)
            ty = cv.opt_default;
        if (ty == TY_NONE)
            cv_err("OPTION DEFAULT NONE: '%s' needs a type", canon);

        if (cv.mode == M_DECL)
            s = declare(canon, ty, scope, dims, ndims, is_static);
        else
            s = sym_lookup(canon);

        if (accept_op("=")) {
            if (s != NULL)
                s->has_init = 1;
            emit_initialiser(s, is_static);
        }

        if (!accept_op(","))
            break;
    }
}

static void emit_initialiser(struct sym *s, int is_static)
{
    const char *guard = NULL;

    if (is_static && cv.mode == M_EMIT && s->is_static) {
        guard = sfmt("__once_%s", pfx_dunder("", s->name));
        emit(sfmt("if (!%s) { %s = 1;", guard, guard));
        cv.indent++;
    }
    if (s->is_array) {
        int k = 0;
        expect_op("(");
        for (;;) {
            struct val v = expr();
            if (cv.mode == M_EMIT) {
                char *sub = linear_index(s, k);
                if (s->ty == TY_S)
                    emit(sfmt("mm_sset(%s, %s);", sub, v.code));
                else if (s->ty == TY_I)
                    emit(sfmt("%s = %s;", sub, as_int(v)));
                else
                    emit(sfmt("%s = %s;", sub, as_flt(v)));
            }
            k++;
            if (!accept_op(","))
                break;
        }
        expect_op(")");
    } else {
        struct val v = expr();
        if (cv.mode == M_EMIT) {
            if (s->ty == TY_S) {
                if (v.ty != TY_S)
                    cv_err("cannot assign a number to '%s'", s->name);
                emit(sfmt("mm_sset(%s, %s);", s->acc, v.code));
            } else if (s->ty == TY_I) {
                emit(sfmt("%s = %s;", s->acc, as_int(v)));
            } else {
                emit(sfmt("%s = %s;", s->acc, as_flt(v)));
            }
        }
    }
    if (guard != NULL) {
        cv.indent--;
        emit("}");
    }
}

/* Element k of an array in an initialiser list. */
static char *linear_index(struct sym *s, int k)
{
    if (s->ndims == 1)
        return sfmt("%s[%d]", s->acc, k);
    cv_err("initialiser lists are only supported for 1-D arrays");
    return NULL;
}

void do_const(void)
{
    for (;;) {
        struct tok *t = nxt();
        char *canon;
        int sfx, ty;
        struct val v;

        if (t->kind != T_ID)
            cv_err("CONST needs a name");
        canon = split_suffix(t->text, &sfx);
        expect_op("=");
        v = expr();
        ty = v.ty;
        if (sfx != TY_NONE && sfx != ty) {
            if (sfx == TY_F && ty == TY_I) {
                v.code = as_flt(v);
                v.ty = TY_F;
                ty = TY_F;
            } else if (sfx == TY_I && ty == TY_F) {
                v.code = as_int(v);
                v.ty = TY_I;
                ty = TY_I;
            } else {
                cv_err("CONST '%s' type conflict", canon);
            }
        }
        if (cv.mode == M_DECL) {
            struct sym *s;
            if (globals_get(canon) != NULL)
                cv_err("'%s' already declared", canon);
            s = palloc(sizeof(*s));
            memset(s, 0, sizeof(*s));
            s->name = pstr(canon);
            s->disp = s->name;
            s->ty = ty;
            s->acc = pstr(sfmt("(%s)", v.code));
            s->is_const = 1;
            s->where = cv.lineno;
            s->declared_in = "";
            GROW(cv.globals, cv.nglobals, cv.cglobals);
            cv.globals[cv.nglobals++] = s;
        }
        if (!accept_op(","))
            break;
    }
}

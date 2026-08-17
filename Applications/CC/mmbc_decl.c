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

int routine_name_known(const char *canon)
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
        /* Every array a REDIM names, wherever it stands.  In MMBasic all
           arrays are allocated at run time so any of them can be
           re-dimensioned; here an array with constant bounds is a C
           array with nothing to change, so being REDIMmed anywhere is
           what makes it dynamic - and the DIM has to know before it is
           translated, which is why this is a pre-scan. */
        for (k = 0; k < nt - 1; k++) {
            int n;
            if (toks[k].kind != T_ID || strcmp(toks[k].up, "REDIM") != 0)
                continue;
            n = k + 1;
            if (n < nt && toks[n].kind == T_ID
                && strcmp(toks[n].up, "PRESERVE") == 0)
                n++;
            while (n + 1 < nt && toks[n].kind == T_ID) {
                int depth = 0;
                char *canon = split_suffix(toks[n].text, &sfx);
                if (!redimmed_in(canon)) {
                    GROW(cv.redimmed, cv.nredimmed, cv.credimmed);
                    cv.redimmed[cv.nredimmed++] = pstr(canon);
                }
                n++;
                while (n < nt) {
                    if (toks[n].kind == T_OP
                        && strcmp(toks[n].text, "(") == 0) {
                        depth++;
                    } else if (toks[n].kind == T_OP
                               && strcmp(toks[n].text, ")") == 0) {
                        if (--depth == 0) { n++; break; }
                    }
                    n++;
                }
                if (n < nt && toks[n].kind == T_OP
                    && strcmp(toks[n].text, ",") == 0)
                    n++;
                else
                    break;
            }
            break;
        }
    }
}

void pass_declarations(void)
{
    volatile int idx;

    cv.mode = M_DECL;
    cv.in_type = 0;
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
    /* CLS is the one statement word that takes no arguments, so a ':'
       after it separates statements and never starts a label.  Without
       this "CLS : PRINT x" defines a label called CLS and drops the
       clear without a word.  Every other statement word is followed by
       an argument, so "NAME :" cannot arise for it. */
    if (t != NULL && t->kind == T_ID && is_op(":", 1)
        && !kw_in(t->up) && builtin_get(t->up) == NULL
        && strcmp(t->up, "CLS") != 0) {
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

    if (skip_type_block(up))
        return;
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
        struct tok *w = peek(0);
        int ty;
        if (w != NULL && w->kind == T_ID) {
            int wsfx;
            if (types_get(split_suffix(w->text, &wsfx)) != NULL) {
                r->ty = TY_F;   /* keep later passes coherent */
                cv_err("a FUNCTION returning a TYPE is not "
                       "translated yet");
            }
        }
        ty = type_word();
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
    const char *stype = NULL;
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
        struct tok *w = peek(0);
        int wsfx;
        if (w != NULL && w->kind == T_ID
            && strcmp(w->up, "INTEGER") != 0
            && strcmp(w->up, "FLOAT") != 0
            && strcmp(w->up, "STRING") != 0
            && types_get(split_suffix(w->text, &wsfx)) != NULL) {
            cv.i += 1;
            stype = pstr(split_suffix(w->text, &wsfx));
            if (sfx != TY_NONE)
                cv_err("parameter type conflict");
            if (has_dims)
                cv_err("whole arrays of structures as parameters "
                       "are not translated yet");
            ty = TY_I;
        } else {
            int ty2 = type_word();
            if (ty != TY_NONE && ty != ty2)
                cv_err("parameter type conflict");
            ty = ty2;
        }
    }
    if (ty == TY_NONE)
        ty = cv.opt_default;
    s = palloc(sizeof(*s));
    memset(s, 0, sizeof(*s));
    s->name = pstr(canon);
    s->disp = s->name;
    s->ty = ty;
    s->stype = stype;
    s->acc = "";
    s->is_param = 1;
    s->byref = byref;
    s->where = cv.lineno;
    s->declared_in = r->name;
    if (stype != NULL) {
        /* a struct parameter is always by reference, as the
         * firmware has it - BYVAL is ignored for structs there too */
        s->byref = 1;
        s->acc = pstr(sfmt("(*%s)", pfx_dunder("p_", canon)));
    } else if (has_dims) {
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
    if (strcmp(t->up, "CONSOLE") == 0) {
        /* OPTION CONSOLE SERIAL | SCREEN | BOTH | NONE
         *
         * A bitmask, exactly the reference's (MM_Misc.c:5178): BOTH 3,
         * SERIAL 1, SCREEN 2, NONE 0, and putConsole is "if
         * (OptionConsole & 2) DisplayPutC; if (OptionConsole & 1)
         * SerialConsolePutC" (PicoMite.c:1174).
         *
         * A run-time statement, not a compile-time setting: a program
         * turns the screen off round a section and back on after, so it
         * emits a call where it stands.
         *
         * This is the debugging tool that was missing.  A program in a
         * graphics mode drew its PRINTs on the screen and nothing
         * reached the console, so a trace either scrolled away under
         * the picture or was overwritten by it - and if the machine
         * then stopped, there was nothing to read anywhere. */
        int mode = -1;

        cv.i++;
        w = nxt();
        if (w->kind == T_ID) {
            if (strcmp(w->up, "SERIAL") == 0)
                mode = 1;
            else if (strcmp(w->up, "SCREEN") == 0)
                mode = 2;
            else if (strcmp(w->up, "BOTH") == 0)
                mode = 3;
            else if (strcmp(w->up, "NONE") == 0)
                mode = 0;
        }
        if (mode < 0)
            cv_err("OPTION CONSOLE wants SERIAL, SCREEN, BOTH or NONE");
        emit(sfmt("mm_console(%d);", mode));
        return;
    }
    skip_statement();
}

static void emit_initialiser(struct sym *s, int is_static);
static char *linear_index(struct sym *s, int k);

/* Runs in every pass.  In the 'decl' pass it records the symbols; in
 * the 'emit' pass it produces the initialisation code (the
 * declarations themselves are hoisted to the top of the C scope). */
/* Was this array named by some REDIM?  In MMBasic every array is
   allocated at run time so any of them can be re-dimensioned; here an
   array with constant bounds is a C array with nothing to change, so
   being REDIMmed anywhere is what makes it dynamic - and the DIM has to
   know before it is translated, which is why pass_routine_names
   collects the names in a pre-scan. */
int redimmed_in(const char *canon)
{
    int k;

    for (k = 0; k < cv.nredimmed; k++)
        if (strcmp(cv.redimmed[k], canon) == 0)
            return 1;
    return 0;
}

/* The C declaration of a run-time array's storage pointer. */
const char *dyn_decl(struct sym *s, const char *cn)
{
    if (s->ty == TY_S)
        return sfmt("char (*%s)[MM_STRSZ]", cn);
    return sfmt("%s *%s", ctype_of(s->ty), cn);
}

/* The C size of one element of an array. */
static const char *elsize_of(struct sym *s)
{
    if (s->ty == TY_S)
        return "MM_STRSZ";
    return sfmt("sizeof(%s)", ctype_of(s->ty));
}

/*
 * DIM / REDIM of an array with run-time bounds.
 *
 * The new bounds go into a scratch table first and the runtime swaps
 * them in, so a REDIM PRESERVE can compare the two before anything is
 * allocated - and so a failed one leaves the array as it was rather
 * than half changed.
 *
 * `dims` holds counts (the declaration adds the +1); the table holds
 * MMBasic's UPPER BOUNDS, which is count - 1.
 */
void emit_dim_alloc(struct sym *s, const char **dims, int ndims,
                    int preserve)
{
    const char *nb = newtmp("nb");
    int k;

    const char *old = newtmp("ao");
    const char *np = newtmp("an");

    cv.tmp_used = 1;
    emit(sfmt("{ MMINTEGER %s[%d]; void *%s, *%s;",
              nb, ndims + 1, old, np));
    emit(sfmt("  %s[0] = %d;", nb, ndims));
    for (k = 0; k < ndims; k++)
        emit(sfmt("  %s[%d] = (%s) - 1;", nb, k + 1, dims[k]));
    /* mm_heap and mm_lfree here rather than inside the runtime: under
       bcrun only a call made BY the program reaches the VM's
       allocator, so a block the native runtime malloc'd would be a
       machine address in a cell the VM owns. */
    emit(sfmt("  %s = mm_heap((unsigned long)"
              "mm_arr_bytes(%s, %s));", np, nb, elsize_of(s)));
    emit(sfmt("  %s = mm_arr_swap(%s, %s, %s, %s, %s, %d);",
              old, s->acc, s->bacc, nb, np, elsize_of(s), preserve));
    /* The PROGRAM stores the new pointer into its own variable: a
       pointer-to-pointer would be written at the host's width into a
       cell the VM sizes, which is a 32-bit slot under bcrun. */
    emit(sfmt("  %s = %s;", s->acc, np));
    emit(sfmt("  if (%s) mm_lfree(%s); }", old, old));
}

/*
 * REDIM [PRESERVE] a(n) [, b(n) ...]
 *
 * MMBasic's cmd_redim.  The array must already exist and must already
 * be dynamic - which, thanks to the pre-scan, it is whenever a REDIM
 * names it.
 */
void do_redim(void)
{
    int preserve = 0;

    if (is_kw("PRESERVE", 0)) {
        cv.i++;
        preserve = 1;
    }
    for (;;) {
        struct tok *t = nxt();
        const char **dims = NULL;
        int ndims = 0, cdims = 0;
        char *canon;
        int sfx;
        struct sym *s;

        if (t->kind != T_ID)
            cv_err("REDIM needs an array name");
        canon = split_suffix(t->text, &sfx);
        s = sym_lookup(canon);
        if (s == NULL || !s->is_array)
            cv_err("'%s' is not an array", canon);
        if (sfx != TY_NONE && sfx != s->ty)
            cv_err("'%s' is %s but used as %s", canon,
                   tyname_of(s->ty), tyname_of(sfx));
        if (s->is_param)
            cv_err("'%s' is a parameter, so its bounds belong to the "
                   "caller", canon);
        if (!s->dynamic)
            cv_err("'%s' was DIMmed with constant bounds, so it has no "
                   "run-time size to change; give its DIM a bound that "
                   "is not a literal or a CONST", canon);
        expect_op("(");
        for (;;) {
            GROW(dims, ndims, cdims);
            dims[ndims++] = pstr(sfmt("(%s) + 1", as_int(expr())));
            if (!accept_op(","))
                break;
        }
        expect_op(")");
        if (cv.mode == M_EMIT)
            emit_dim_alloc(s, dims, ndims, preserve);
        if (!accept_op(","))
            break;
    }
}

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
        const char *stype = NULL;
        struct sym *s;
        int dyn = 0;

        t = nxt();
        if (t->kind != T_ID)
            cv_err("variable name expected in %s", kw);
        canon = split_suffix(t->text, &sfx);
        if (accept_op("(")) {
            for (;;) {
                struct val v = expr();
                const char *b = as_int(v);
                /* Only the declaration pass matters: it is the one that
                 * captures the bounds, and the one where a CONST still
                 * carries its literal text (by the emit pass it has
                 * become the #define's name). */
                /* A bound that is not a compile-time constant makes
                 * the whole array DYNAMIC: it cannot be a C array,
                 * because the bounds would have to be in its type, so
                 * it becomes a flat pointer plus a bounds table -
                 * which is what an array parameter already is here. */
                if (!const_c_expr(b))
                    dyn = 1;
                GROW(dims, ndims, cdims);
                dims[ndims++] = sfmt("(%s) + 1", b);
                if (!accept_op(","))
                    break;
            }
            expect_op(")");
        }
        ty = (sfx != TY_NONE) ? sfx : group_ty;
        if (accept_kw("AS")) {
            struct tok *w = peek(0);
            int wsfx;
            if (w != NULL && w->kind == T_ID
                && strcmp(w->up, "INTEGER") != 0
                && strcmp(w->up, "FLOAT") != 0
                && strcmp(w->up, "STRING") != 0
                && types_get(split_suffix(w->text, &wsfx)) != NULL) {
                cv.i += 1;
                stype = split_suffix(w->text, &wsfx);
                if (sfx != TY_NONE)
                    cv_err("'%s' has a type suffix but is "
                           "declared AS a TYPE", canon);
                if (is_static)
                    cv_err("STATIC of a TYPE is not translated "
                           "yet; use DIM or LOCAL");
            } else {
                int ty2 = type_word();
                if (ty != TY_NONE && ty != ty2)
                    cv_err("conflicting types for '%s'", canon);
                ty = ty2;
            }
        }
        if (stype == NULL) {
            if (ty == TY_NONE)
                ty = cv.opt_default;
            if (ty == TY_NONE)
                cv_err("OPTION DEFAULT NONE: '%s' needs a type",
                       canon);
        }

        /* MMBasic's DIM s$ LENGTH n, which caps a string to save
           memory.  ACCEPTED AND IGNORED: every string here is MM_STRSZ,
           so this translation is more generous than MMBasic rather than
           different from it - a program that would hit "string too
           long" there simply works.  That is a divergence and the
           manual says so; refusing outright would stop real programs
           translating over a declaration whose only effect is to make a
           string smaller. */
        if (accept_kw("LENGTH")) {
            struct tok *v;

            if (ty != TY_S)
                cv_err("LENGTH is only for strings, and '%s' is not one",
                       canon);
            v = nxt();
            if (v->kind != T_NUM || strcmp(v->up, "I") != 0)
                cv_err("LENGTH takes a literal integer");
            else if (atoi(v->text) < 1 || atoi(v->text) > 255)
                cv_err("LENGTH must be 1..255");
        }

        if (cv.mode == M_DECL) {
            s = declare(canon, stype == NULL ? ty : TY_I,
                        scope, dims, ndims, is_static);
            if (stype != NULL)
                s->stype = pstr(stype);
            if (dyn || redimmed_in(canon)) {
                dyn = 1;
                if (stype != NULL)
                    cv_err("an array of a TYPE needs constant bounds");
                if (ndims > 5)
                    cv_err("an array has at most 5 dimensions");
                s->dynamic = 1;
            }
        } else {
            s = sym_lookup(canon);
        }

        /* A run-time bound is allocated where the DIM stands, not
           hoisted: the expression may name variables that are only set
           by the time the statement runs. */
        if (s != NULL && s->dynamic && cv.mode == M_EMIT)
            emit_dim_alloc(s, dims, ndims, 0);

        if (accept_op("=")) {
            if (s != NULL && s->stype != NULL) {
                struct_initialiser(s);
            } else {
                if (s != NULL)
                    s->has_init = 1;
                emit_initialiser(s, is_static);
            }
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

/* Element k of an array in an initialiser list, in MMBasic's storage
 * order.
 *
 * cmd_dim (Commands.c:8658) fills the values into linear array memory,
 * and MMBasic arrays store the FIRST subscript varying fastest - so
 * DIM a(3,1) = (p,q,...) sets a(0,0), a(1,0), a(2,0), a(3,0), a(0,1),
 * ...  Our C arrays are declared the other way round (the last
 * subscript is adjacent), so the flat position maps to a subscript
 * LIST rather than to a flat offset.  The divisions below are built
 * from k (a literal) and the dimension sizes (constant expressions by
 * the time an array is static), so cc1 folds every one of them to a
 * plain index. */
static char *linear_index(struct sym *s, int k)
{
    char *out;
    const char *div;
    int j;

    if (s->ndims == 1)
        return sfmt("%s[%d]", s->acc, k);
    if (s->dynamic) {
        cv_err("an initialiser list on a run-time DIM is only "
               "supported for 1-D arrays");
        return NULL;
    }
    out = sfmt("%s", s->acc);
    div = NULL;
    for (j = 0; j < s->ndims; j++) {
        const char *sz = s->dims[j];
        char *e;

        if (div == NULL)
            e = sfmt("%d", k);
        else
            e = sfmt("(%d) / (%s)", k, div);
        if (j < s->ndims - 1)
            e = sfmt("(%s) %% (%s)", e, sz);
        out = sfmt("%s[(%s)]", out, e);
        div = (div == NULL) ? sz : sfmt("(%s) * (%s)", div, sz);
    }
    return out;
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
        if (cv.cur != NULL) {
            /* CONST INSIDE A SUB OR FUNCTION IS LOCAL TO IT.
             *
             * MMBasic says so in one line - cmd_const does
             * `if (g_LocalIndex != 0) type |= V_LOCAL;`
             * (Commands.c:6478) - and this used to put every CONST in
             * the globals whatever scope it was written in.  Two
             * routines each declaring their own `Const f$` then
             * collided, and an unrelated `f%` elsewhere in the program
             * failed with "'f' is STRING but used as INTEGER".
             *
             * A local one is a LOCAL assigned where the statement
             * stands and refused as an assignment target afterwards -
             * not a #define.  MMBasic evaluates the expression ONCE,
             * when the statement runs, and it may call a function.
             */
            struct sym *s;
            if (cv.mode == M_DECL) {
                s = declare(canon, ty, "local", NULL, 0, 0);
                s->is_const = 1;
            } else {
                s = sym_lookup(canon);
            }
            if (cv.mode == M_EMIT && s != NULL) {
                if (ty == TY_S)
                    emit(sfmt("mm_sset(%s, %s);", s->acc, v.code));
                else
                    emit(sfmt("%s = %s;", s->acc, v.code));
            }
        } else if (cv.mode == M_DECL) {
            struct sym *s;
            struct sym *dup = globals_get(canon);
            if (dup != NULL) {
                /* A WARNING, and it has to be: MMBasic runs only one
                   arm of an If, so the same global CONST declared in
                   both arms is legal there and the arm that ran is the
                   one that exists.  A compiler sees both and has to
                   pick, and picking silently is how PicoMan came to
                   draw its maze at (200,200) on a 320x240 screen.
                   cv_err would be worse than useless - the declaration
                   pass swallows it, so nothing was printed at all. */
                cv_warn("'%s' is declared CONST more than once; the "
                        "first (line %d) is the one used", canon,
                        dup->where);
                if (!accept_op(","))
                    break;
                continue;
            }
            s = palloc(sizeof(*s));
            memset(s, 0, sizeof(*s));
            s->name = pstr(canon);
            s->disp = s->name;
            s->ty = ty;
            s->acc = pstr(sfmt("(%s)", v.code));
            s->is_const = 1;
            /* An expression that is not compile-time constant must be
               evaluated ONCE, where the statement stands, as
               cmd_const's DoExpression does - never re-evaluated from
               a #define at every use (see const_or_literal_expr) */
            s->const_runtime = !const_or_literal_expr(v.code);
            s->where = cv.lineno;
            s->declared_in = "";
            GROW(cv.globals, cv.nglobals, cv.cglobals);
            cv.globals[cv.nglobals++] = s;
        } else if (cv.mode == M_EMIT) {
            struct sym *s = globals_get(canon);

            if (s != NULL && s->is_const && s->const_runtime) {
                /* evaluate once, in flow: the hidden global takes the
                   value here and every use just reads it */
                if (s->ty == TY_S)
                    emit(sfmt("mm_sset(%s, %s);", cconst(canon),
                              v.code));
                else if (s->ty == TY_I)
                    emit(sfmt("%s = %s;", cconst(canon), as_int(v)));
                else
                    emit(sfmt("%s = %s;", cconst(canon), as_flt(v)));
            }
        }
        if (!accept_op(","))
            break;
    }
}

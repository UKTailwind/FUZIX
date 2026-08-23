/* mmbc_sym.c - Conv plumbing: token access, emission, error/warning
 * lists, symbol tables and the implied-global rule.
 *
 * Mirrors mmb2c.py Conv.err through Conv.note_touch plus as_int/as_flt.
 * Error message text is part of byte identity (skip reasons land in
 * the generated C's header comment) - keep every string exact. */

#include "mmbc.h"

struct conv cv;

/* ---- error / warning lists ---- */

void errors_add(const char *msg)
{
    GROW(cv.errors, cv.nerrors, cv.cerrors);
    cv.errors[cv.nerrors++] = pstr(msg);
}

void errors_add_dedup(const char *msg)
{
    int k;
    for (k = 0; k < cv.nerrors; k++)
        if (strcmp(cv.errors[k], msg) == 0)
            return;
    errors_add(msg);
}

/* self.err: raise MMError("line %d: %s") */
void cv_err(const char *fmt, ...)
{
    char buf[400];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    mm_error("line %d: %s", cv.lineno, buf);
}

/* self.note: an error that must not stop the parse.  Recorded exactly
 * as cv_err would record it, but the statement is allowed to finish so
 * one bad line does not cascade into twenty. */
void cv_note(const char *fmt, ...)
{
    char buf[400];
    char text[448];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    snprintf(text, sizeof(text), "line %d: %s", cv.lineno, buf);
    errors_add_dedup(text);
}

/* self.warn: "line %d: %s", appended once */
void cv_warn(const char *fmt, ...)
{
    char buf[400];
    char text[448];
    va_list ap;
    int k;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    snprintf(text, sizeof(text), "line %d: %s", cv.lineno, buf);
    for (k = 0; k < cv.nwarnings; k++)
        if (strcmp(cv.warnings[k], text) == 0)
            return;
    GROW(cv.warnings, cv.nwarnings, cv.cwarnings);
    cv.warnings[cv.nwarnings++] = pstr(text);
}

/* ---- token access ---- */

struct tok *peek(int k)
{
    int j = cv.i + k;
    if (j >= 0 && j < cv.ntoks)
        return &cv.toks[j];
    return NULL;
}

int at_end(void)
{
    return cv.i >= cv.ntoks;
}

struct tok *nxt(void)
{
    struct tok *t = peek(0);
    if (t == NULL)
        cv_err("unexpected end of line");
    cv.i++;
    return t;
}

int is_op(const char *s, int k)
{
    struct tok *t = peek(k);
    return t != NULL && t->kind == T_OP && strcmp(t->text, s) == 0;
}

int is_kw(const char *s, int k)
{
    struct tok *t = peek(k);
    return t != NULL && t->kind == T_ID && strcmp(t->up, s) == 0;
}

int accept_op(const char *s)
{
    if (is_op(s, 0)) {
        cv.i++;
        return 1;
    }
    return 0;
}

void expect_op(const char *s)
{
    /* "syntax error" first, because that is what it is and what MMBasic
       calls it.  A bare "expected ')'" reads as a missing bracket and
       sends the reader looking for one; the real cause is usually an
       argument form this translator does not take yet, with the ')'
       simply being where it gave up. */
    if (!accept_op(s))
        cv_err("syntax error (expected '%s')", s);
}

int accept_kw(const char *s)
{
    if (is_kw(s, 0)) {
        cv.i++;
        return 1;
    }
    return 0;
}

/* Does this text parse as a justification?  mmg_just's grammar and
   MMBasic's GetJustification: [L|C|R] then [T|M|B] then [N|V|I|U|D],
   each of them optional. */
static int looks_like_just(const char *s)
{
    static const char *sets[] = { "LCR", "TMB", "NVIUD" };
    int i = 0, k;
    char *u = upper(s);
    int n = (int)strlen(u);

    for (k = 0; k < 3; k++)
        if (i < n && strchr(sets[k], u[i]) != NULL)
            i++;
    return n > 0 && i == n;
}

/* TEXT's justification: a bare word or a string.

   MMBasic tries the argument's RAW TEXT as a justification before it
   evaluates anything (Draw.c:2148-2149), which is what makes
   `TEXT x, y, s$, CM' work unquoted - picofrog writes it that way and
   so does most PicoMite code.  The ambiguity that comes with it is the
   reference's too: a variable called C loses to the justification C,
   there and here.

   Only when the word IS the whole argument: `C + "M"' has to be
   evaluated, and MMBasic tries its whole text first for the same
   reason. */
const char *just_arg(void)
{
    struct tok *t = peek(0);

    if (t != NULL && t->kind == T_ID && looks_like_just(t->text)) {
        struct tok *n = peek(1);

        if (n == NULL ||
            (n->kind == T_OP && (strcmp(n->up, ",") == 0 ||
                                 strcmp(n->up, ":") == 0)) ||
            (n->kind == T_ID && strcmp(n->up, "ELSE") == 0)) {
            cv.i += 1;
            return c_string_literal(t->text);
        }
    }
    return as_str(expr());
}

/* A bare letter, a quoted letter, or a string at run time.

   MMBasic's own two-stage form, and both stages are in its source for
   each of these: cmd_framebuffer and cmd_play try checkstring()
   against the bare token first and fall through to getCstring() +
   strcasecmp() - which is why "b" and "B" and a bare B all work, and
   why a variable is allowed where the manual only ever shows a letter.

   A quoted letter is decided HERE rather than at run time: it is
   knowable now, and an unknown one is then a translation error instead
   of something the program discovers when it plays. */
const char *kw_or_str(const struct kwval *table, const char *rt,
                      const char *what)
{
    const struct kwval *k;
    struct tok *t;

    for (k = table; k->nm; k++)
        if (is_kw(k->nm, 0)) {
            cv.i += 1;
            return sfmt("%d", k->val);
        }
    t = peek(0);
    if (t != NULL && t->kind == T_STR) {
        /* .strip().upper(), as the Python does */
        const char *b = t->text;
        const char *e;
        char *cut;
        size_t n;
        char *up;

        while (*b == ' ' || *b == '\t')
            b++;
        e = b + strlen(b);
        while (e > b && (e[-1] == ' ' || e[-1] == '\t'))
            e--;
        n = (size_t)(e - b);
        cut = salloc(n + 1);
        memcpy(cut, b, n);
        cut[n] = 0;
        up = upper(cut);
        for (k = table; k->nm; k++)
            if (strcmp(up, k->nm) == 0) {
                cv.i += 1;
                return sfmt("%d", k->val);
            }
        cv_err("%s", what);
    }
    return sfmt("%s(%s)", rt, as_str(expr()));
}

/* Which framebuffer a FRAMEBUFFER argument names, as a C expression.
   N is the screen, F the off-screen buffer and L the layer - which is
   just a second off-screen buffer, and becomes a layer only in MERGE.
   MMBasic's T and 2 name buffers this machine does not have, so they
   are refused here rather than quietly becoming one of these three. */
const char *fb_buf(void)
{
    static const struct kwval bufs[] = {
        { "N", 0 }, { "F", 1 }, { "L", 2 }, { NULL, 0 }
    };
    const char *e = kw_or_str(bufs, "__mmb_fbsel", "expected N, F or L");

    if (!(e[0] >= '0' && e[0] <= '9'))
        cv.uses_fbsel = 1;
    return e;
}

/* ELSE terminates a statement too (single-line IF). */
int stmt_end(void)
{
    return at_end() || is_op(":", 0) || is_kw("ELSE", 0);
}

/* ---- emission ---- */

void out_append(struct outbuf *o, const char *line)
{
    GROW(o->lines, o->n, o->cap);
    o->lines[o->n++] = line;
}

void out_insert(struct outbuf *o, int where, const char *line)
{
    GROW(o->lines, o->n, o->cap);
    memmove(o->lines + where + 1, o->lines + where,
            sizeof(*o->lines) * (size_t)(o->n - where));
    o->lines[where] = line;
    o->n++;
}

void emit(const char *text)
{
    char *ln;
    int k;

    if (cv.mode != M_EMIT)
        return;
    ln = palloc((size_t)cv.indent * 4 + strlen(text) + 1);
    for (k = 0; k < cv.indent * 4; k++)
        ln[k] = ' ';
    strcpy(ln + k, text);
    out_append(cv.out, ln);
}

/* Index of the line emit() just wrote, or -1 outside emission.
 *
 * For patching a call after the fact - see do_print, which turns the
 * last item of a PRINT into its flushing variant. */
int last_line(void)
{
    if (cv.mode != M_EMIT)
        return -1;
    return cv.out->n - 1;
}

void raw(const char *text)
{
    if (cv.mode != M_EMIT)
        return;
    out_append(cv.out, pstr(text));
}

char *newtmp(const char *pfx)
{
    cv.tmpn++;
    return sfmt("__%s%d", pfx, cv.tmpn);
}

/* ---- labels: find-or-create the record for one name ---- */

struct label *label_rec(const char *canon)
{
    int k;
    struct label *l;

    for (k = 0; k < cv.nlabels; k++)
        if (strcmp(cv.labels[k].name, canon) == 0)
            return &cv.labels[k];
    GROW(cv.labels, cv.nlabels, cv.clabels);
    l = &cv.labels[cv.nlabels++];
    memset(l, 0, sizeof(*l));
    l->name = pstr(canon);
    return l;
}

/* ---- symbol lookup / creation ---- */

static struct sym *table_get(struct sym **tab, int n, const char *canon)
{
    int k;
    for (k = 0; k < n; k++)
        if (strcmp(tab[k]->name, canon) == 0)
            return tab[k];
    return NULL;
}

/* Local (or param) first, then global.  NULL if unknown. */
struct sym *sym_lookup(const char *canon)
{
    struct sym *s;

    if (cv.cur != NULL) {
        s = table_get(cv.cur->locals, cv.cur->nlocals, canon);
        if (s != NULL)
            return s;
    }
    return table_get(cv.globals, cv.nglobals, canon);
}

struct sym *sym_new(const char *canon, int ty, const char *acc)
{
    struct sym *s = palloc(sizeof(*s));

    memset(s, 0, sizeof(*s));
    s->name = pstr(canon);
    s->disp = s->name;
    s->ty = ty;
    s->acc = pstr(acc);
    s->declared_in = "";
    return s;
}

/* Explicit DIM / LOCAL / STATIC / CONST / parameter. */
struct sym *declare(const char *canon, int ty, const char *scope,
                    const char **arr_dims, int ndims, int is_static)
{
    int local = strcmp(scope, "local") == 0;
    struct sym *old, *s;
    const struct builtin *b;
    int pass;

    for (pass = 0; pass < 2; pass++) {
        /* only built-ins that can appear without brackets are genuinely
         * reserved; MIN, LEN and friends are told apart from a variable
         * by the '(' that follows them */
        char *nm = upper(pass ? sfmt("%s$", canon) : sstr(canon));
        b = builtin_get(nm);
        if (b != NULL && b->minargs == 0)
            cv_err("'%s' is a built-in function and cannot be used as "
                   "a variable name", canon);
    }
    old = local ? table_get(cv.cur->locals, cv.cur->nlocals, canon)
                : table_get(cv.globals, cv.nglobals, canon);
    if (old != NULL) {
        if (old->ty != ty)
            cv_err("'%s' already declared as %s", canon,
                   tyname_of(old->ty));
        return old;
    }
    s = sym_new(canon, ty, cvar(canon));
    s->where = cv.lineno;
    s->is_static = is_static;
    s->declared_in = (local && cv.cur) ? cv.cur->name : "";
    if (arr_dims != NULL) {
        int k;
        s->is_array = 1;
        s->ndims = ndims;
        s->dims = palloc(sizeof(char *) * (size_t)(ndims ? ndims : 1));
        for (k = 0; k < ndims; k++)
            s->dims[k] = pstr(arr_dims[k]);
    }
    if (local) {
        GROW(cv.cur->locals, cv.cur->nlocals, cv.cur->clocals);
        cv.cur->locals[cv.cur->nlocals++] = s;
        GROW(cv.cur->local_order, cv.cur->nlocal_order,
             cv.cur->clocal_order);
        cv.cur->local_order[cv.cur->nlocal_order++] = s->name;
        if (is_static) {
            GROW(cv.cur->statics, cv.cur->nstatics, cv.cur->cstatics);
            cv.cur->statics[cv.cur->nstatics++] = s;
        }
    } else {
        GROW(cv.globals, cv.nglobals, cv.cglobals);
        cv.globals[cv.nglobals++] = s;
    }
    return s;
}

/* A name used in an expression or as an assignment target.  If it is
 * not already known then MMBasic creates it, right here, as a GLOBAL -
 * even inside a subroutine.  The implied-declaration rule; this is the
 * one place it happens. */
struct sym *reference(const char *word, int as_array)
{
    int sfx;
    char *canon = split_suffix(word, &sfx);
    struct sym *s = sym_lookup(canon);
    int ty;

    if (s != NULL) {
        if (sfx != TY_NONE && sfx != s->ty)
            cv_err("'%s' is %s but used as %s", canon,
                   tyname_of(s->ty), tyname_of(sfx));
        note_touch(canon, s);
        return s;
    }
    /* MM. is MMBasic's own namespace, not the program's: a name in it
     * is a read we have not translated, and turning it into an implied
     * variable makes it answer 0 for ever.  MM.WIDTH did exactly that -
     * it compiled clean and printed 0 where a PicoMite prints 80.  The
     * translated ones never reach here; they are matched in the builtin
     * path above. */
    if (strncmp(canon, "mm.", 3) == 0)
        cv_err("%s is not translated", word);
    if (as_array)
        cv_err("array '%s' used but never DIMensioned", canon);
    ty = (sfx != TY_NONE) ? sfx : cv.opt_default;
    if (ty == TY_NONE)
        cv_err("OPTION DEFAULT NONE: '%s' has no type", canon);
    if (cv.opt_explicit)
        cv_err("OPTION EXPLICIT: '%s' has not been declared", canon);
    if (cv.mode != M_EMIT) {
        struct implied_rec *r;
        s = sym_new(canon, ty, cvar(canon));
        s->disp = pstr(word);
        s->where = cv.lineno;
        s->implied = 1;
        GROW(cv.globals, cv.nglobals, cv.cglobals);
        cv.globals[cv.nglobals++] = s;
        GROW(cv.implied, cv.nimplied, cv.cimplied);
        r = &cv.implied[cv.nimplied++];
        r->name = s->name;
        r->ty = ty;
        r->line = cv.lineno;
        r->routine = cv.cur ? cv.cur->name : "";
        note_touch(canon, s);
        return s;
    }
    cv_err("internal: '%s' unresolved in emit pass", canon);
    return NULL;
}

/* Remember that this SUB/FUNCTION reached out to a global. */
void note_touch(const char *canon, struct sym *s)
{
    int k;

    if (cv.mode != M_SCAN || cv.cur == NULL)
        return;
    if (s->is_const || s->is_param)
        return;
    if (table_get(cv.cur->locals, cv.cur->nlocals, canon) != NULL)
        return;
    for (k = 0; k < cv.cur->ngtouch; k++)
        if (strcmp(cv.cur->gtouch[k].name, canon) == 0)
            return;
    GROW(cv.cur->gtouch, cv.cur->ngtouch, cv.cur->cgtouch);
    cv.cur->gtouch[cv.cur->ngtouch].name = pstr(canon);
    cv.cur->gtouch[cv.cur->ngtouch].line = cv.lineno;
    cv.cur->ngtouch++;
}

/* ---- value coercions ---- */

const char *as_int(struct val v)
{
    if (v.ty == TY_I)
        return v.code;
    if (v.ty == TY_F)
        return sfmt("mm_toint(%s)", v.code);
    if (v.ty == TY_T)
        cv_err("a whole structure cannot be used in an expression");
    cv_err("string used where a number is required");
    return NULL;
}

const char *as_flt(struct val v)
{
    if (v.ty == TY_F)
        return v.code;
    if (v.ty == TY_I) {
        const char *f = float_form_of_int_literal(v.code);
        return f != NULL ? f : sfmt("(MMFLOAT)(%s)", v.code);
    }
    if (v.ty == TY_T)
        cv_err("a whole structure cannot be used in an expression");
    cv_err("string used where a number is required");
    return NULL;
}

/* An MMBasic string - length byte, data, NUL - not a C one.  The callee
   is expected to know that and use mm_slen/mm_cstr. */
const char *as_str(struct val v)
{
    if (v.ty == TY_S)
        return v.code;
    if (v.ty == TY_T)
        cv_err("a whole structure cannot be used in an expression");
    cv_err("number used where a string is required");
    return NULL;
}

struct val need_num(struct val v)
{
    if (v.ty == TY_S)
        cv_err("string used where a number is required");
    return v;
}

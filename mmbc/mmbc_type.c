/* mmbc_type.c - TYPE ... END TYPE, structures and STRUCT.
 *
 * Mirrors mmb2c.py TypeMember/TypeDef (317-370), the structure member
 * access region (member_path 745 - member_value 852), struct_fn
 * (1069), pass_types/type_statement/type_member/skip_type_block
 * (1837-1986) and assign_member/assign_struct/struct_operand/
 * do_struct/struct_initialiser (4042-4193).  Generated text and error
 * messages are byte identity - keep every string exact. */

#include "mmbc.h"
#include "mmbc_expr.h"

/* Python int(text) for a token already validated as literal digits.
 * The Fuzix libc has no strtoll, and these are the only 64-bit parses
 * in the translator. */
static long long ll_digits(const char *p)
{
    long long v = 0;
    while (*p >= '0' && *p <= '9')
        v = v * 10 + (*p++ - '0');
    return v;
}

/* ---- the types table: self.types + self.type_order in one ---- */

struct typedef_rec *types_get(const char *canon)
{
    int k;
    for (k = 0; k < cv.ntypes; k++)
        if (strcmp(cv.types[k]->name, canon) == 0)
            return cv.types[k];
    return NULL;
}

/* td.byname.get(name) */
static struct typemember *member_get(struct typedef_rec *td,
                                     const char *name)
{
    int k;
    for (k = 0; k < td->nmembers; k++)
        if (strcmp(td->members[k]->name, name) == 0)
            return td->members[k];
    return NULL;
}

/* canon.split('.') - scratch segments, empty ones kept like Python's */
static const char **split_dots(const char *s, int *np)
{
    int n = 1, k = 0;
    const char *p;
    const char **out;

    for (p = s; *p; p++)
        if (*p == '.')
            n++;
    out = salloc(sizeof(char *) * (size_t)n);
    p = s;
    for (;;) {
        const char *q = strchr(p, '.');
        size_t len = q ? (size_t)(q - p) : strlen(p);
        char *seg = salloc(len + 1);
        memcpy(seg, p, len);
        seg[len] = 0;
        out[k++] = seg;
        if (q == NULL)
            break;
        p = q + 1;
    }
    *np = n;
    return out;
}

/* ---- the layout arithmetic: TypeDef.add / TypeDef.close ---- */

static void td_add(struct typedef_rec *td, struct typemember *m)
{
    long long off;
    int k;

    if (m->ty == TY_S) {
        m->esize = m->slen + 1;
    } else if (m->stype != NULL) {
        struct typedef_rec *inner = types_get(m->stype);
        m->esize = inner->total;
        if (inner->numeric)
            td->numeric = 1;
    } else {
        m->esize = 8;
        td->numeric = 1;
    }
    off = td->total;
    if ((m->ty == TY_I || m->ty == TY_F || m->stype != NULL) && off % 8)
        off = (off / 8 + 1) * 8;
    m->offset = off;
    m->count = 1;
    if (m->has_dims)
        for (k = 0; k < m->ndims; k++)
            m->count *= m->dims[k] + 1;
    td->total = off + m->esize * m->count;
    td->members[td->nmembers++] = m;
}

static void td_close(struct typedef_rec *td)
{
    if (td->numeric && td->total % 8)
        td->total = (td->total / 8 + 1) * 8;
}

/* ---- structure member access ---------------------------------------
 *
 * A dotted identifier is ONE token (dots are name characters), so
 * p.x arrives whole and the firmware's rule applies at lookup time:
 * split at the first dot, and if the prefix names a struct variable
 * the rest is a member path.  Indices interrupt a path as separate
 * tokens - v.a(i).b is  ID('v.a') '(' i ')' '.' ID('b')  - which is
 * why the walker below alternates between parts of the current
 * token and fresh tokens fetched after ')' when a '.' follows.
 */

/* ( i [, j ...] ) on a member array -> the linear index, the
 * same linearisation MMBasic uses. */
static const char *member_index(struct typemember *m)
{
    const char *idx[MAXARGS];
    int n = 0, k;
    const char *lin;
    long long mult;

    expect_op("(");
    for (;;) {
        if (n >= MAXARGS)
            mm_error("line %d: too many subscripts", cv.lineno);
        idx[n++] = as_int(expr());
        if (!accept_op(","))
            break;
    }
    expect_op(")");
    if (n != m->ndims)
        cv_err("member '%s' has %d dimension(s), %d given",
               m->name, m->ndims, n);
    lin = sfmt("(%s)", idx[0]);
    mult = 1;
    for (k = 1; k < n; k++) {
        mult *= m->dims[k - 1] + 1;
        lin = sfmt("%s + (%s) * %lld", lin, idx[k], mult);
    }
    return lin;
}

/* Walk a member path from a struct lvalue. */
struct mpres member_path(const char *base, const char *tyname,
                         const char **parts, int nparts, int sfx)
{
    int via = 0;
    int pi = 0;
    struct mpres res;

    memset(&res, 0, sizeof(res));
    for (;;) {
        struct typedef_rec *td;
        const char *name;
        struct typemember *m;
        const char *code;
        const char *lin;

        if (pi >= nparts) {
            if (is_op(".", 0)) {
                struct tok *t;
                char *canon;
                cv.i += 1;
                t = nxt();
                if (t->kind != T_ID)
                    cv_err("member name expected after '.'");
                canon = split_suffix(t->text, &sfx);
                parts = split_dots(canon, &nparts);
                pi = 0;
                continue;
            }
            res.kind = MP_STRUCT;
            res.code = base;
            res.tyname = tyname;
            res.via = via;
            return res;
        }
        td = types_get(tyname);
        name = parts[pi++];
        m = member_get(td, name);
        if (m == NULL)
            cv_err("'%s' is not a member of TYPE '%s'", name, td->disp);
        via = 1;
        code = sfmt("%s.m_%s", base, name);
        lin = NULL;
        if (m->has_dims) {
            /* the index can only follow the LAST part of the token */
            if (pi >= nparts && is_op("(", 0))
                lin = member_index(m);
            else if (m->stype != NULL)
                cv_err("an array of nested structures needs an "
                       "index");
            else
                cv_err("array member '%s' needs an index", name);
        } else if (pi >= nparts && is_op("(", 0) && m->stype == NULL) {
            cv_err("member '%s' is not an array", name);
        }
        if (m->stype != NULL) {
            if (lin != NULL)
                code = sfmt("%s[(int)(%s)]", code, lin);
            if (pi < nparts || is_op(".", 0)) {
                base = code;
                tyname = m->stype;
                continue;
            }
            res.kind = MP_STRUCT;
            res.code = code;
            res.tyname = m->stype;
            res.via = 1;
            return res;
        }
        /* a plain member ends the walk */
        if (pi < nparts)
            cv_err("'%s' is not a nested structure", name);
        if (m->ty == TY_S) {
            if (lin != NULL)
                code = sfmt("(%s + (int)(%s) * %lld)", code, lin,
                            m->slen + 1);
            if (sfx != TY_NONE && sfx != TY_S)
                cv_err("member '%s' is a STRING", name);
            res.kind = MP_STR;
            res.code = code;
            res.slen = m->slen;
            return res;
        }
        if (lin != NULL)
            code = sfmt("%s[(int)(%s)]", code, lin);
        if (sfx != TY_NONE && sfx != m->ty)
            cv_err("member '%s' is %s", name, tyname_of(m->ty));
        res.kind = MP_NUM;
        res.code = code;
        res.ty = m->ty;
        return res;
    }
}

/* The dotted-identifier entry: split at the first dot and fill *out
 * when the prefix is a struct variable, else return 0 (the name stays
 * a plain dotted variable). */
int struct_head(const char *word, struct shead *out)
{
    int sfx;
    char *canon = split_suffix(word, &sfx);
    const char *dot = strchr(canon, '.');
    char *head;
    struct sym *s;
    int np;

    if (dot == NULL)
        return 0;
    head = salloc((size_t)(dot - canon) + 1);
    memcpy(head, canon, (size_t)(dot - canon));
    head[dot - canon] = 0;
    s = sym_lookup(head);
    if (s == NULL || s->stype == NULL)
        return 0;
    if (sym_lookup(canon) != NULL)
        cv_err("'%s' is shadowed by struct variable '%s' - the "
               "firmware would make it unreachable", canon, head);
    note_touch(head, s);
    out->s = s;
    out->parts = split_dots(canon, &np) + 1;    /* [1:] */
    out->nparts = np - 1;
    out->sfx = sfx;
    return 1;
}

/* The C lvalue for a struct variable, consuming an element index when
 * it is an array. */
const char *struct_base(struct sym *s)
{
    if (s->is_array) {
        if (!is_op("(", 0))
            cv_err("struct array '%s' needs an index here", s->name);
        return index_of(s);
    }
    return s->acc;
}

/* member_path result -> an expression (code, ty).  A string member is
 * copied to a scratch temp: member strings have no trailing NUL (the
 * firmware's layout has no room for one), and the copy restores the
 * invariant every consumer assumes. */
struct val member_value(struct mpres res)
{
    struct val v;

    v.stype = NULL;
    v.tm = 0;
    if (res.kind == MP_NUM) {
        v.code = res.code;
        v.ty = res.ty;
        return v;
    }
    if (res.kind == MP_STR) {
        cv.tmp_used = 1;
        v.code = sfmt("mm_scopy(%s)", res.code);
        v.ty = TY_S;
        return v;
    }
    v.code = res.code;
    v.ty = TY_T;
    v.stype = res.tyname;
    v.tm = res.via;
    return v;
}

/* STRUCT(SIZEOF t$) / STRUCT(OFFSET t$, m$) / STRUCT(TYPE t$, m$) -
 * the layout is fixed at translation time, so with literal names
 * these are compile-time constants.  STRUCT(FIND) needs a runtime
 * search and is not translated yet. */
struct val struct_fn(void)
{
    struct tok *t, *a, *b;
    const char *sel;
    char *tc;
    struct typedef_rec *td;
    struct typemember *m;
    struct val v;

    v.ty = TY_I;
    v.stype = NULL;
    v.tm = 0;
    expect_op("(");
    t = nxt();
    sel = (t->kind == T_ID) ? t->up : "";
    if (strcmp(sel, "FIND") == 0)
        cv_err("STRUCT(FIND ...) is not translated yet");
    if (strcmp(sel, "SIZEOF") != 0 && strcmp(sel, "OFFSET") != 0
        && strcmp(sel, "TYPE") != 0)
        cv_err("unknown STRUCT( selector '%s'", t->text);
    a = nxt();
    if (a->kind != T_STR)
        cv_err("STRUCT(%s ...) takes a literal string type name "
               "here", sel);
    tc = lower(a->text);
    td = types_get(tc);
    if (td == NULL)
        cv_err("structure type '%s' not found", a->text);
    if (strcmp(sel, "SIZEOF") == 0) {
        expect_op(")");
        v.code = sfmt("%lldLL", td->total);
        return v;
    }
    expect_op(",");
    b = nxt();
    if (b->kind != T_STR)
        cv_err("STRUCT(%s ...) takes a literal member name here",
               sel);
    m = member_get(td, lower(b->text));
    if (m == NULL)
        cv_err("member '%s' not found in structure '%s'",
               b->text, a->text);
    expect_op(")");
    if (strcmp(sel, "OFFSET") == 0) {
        v.code = sfmt("%lldLL", m->offset);
        return v;
    }
    if (m->stype != NULL) {
        v.code = "0LL";               /* the firmware masks T_STRUCT out */
        return v;
    }
    v.code = sfmt("%dLL", m->ty == TY_F ? 1 : m->ty == TY_S ? 2 : 4);
    return v;
}

/* ---- the pre-pass: register every TYPE ... END TYPE ---- */

/* member [ (d1[,d2...]) ] AS INTEGER|INT|FLOAT|
 * STRING [LENGTH n] | <earlier typename> */
static struct typedef_rec *type_member(struct typedef_rec *td)
{
    struct tok *t = nxt();
    char *canon;
    int sfx;
    struct typemember *m;
    struct tok *w;

    if (t->kind != T_ID)
        cv_err("member declaration expected inside TYPE");
    canon = split_suffix(t->text, &sfx);
    if (sfx != TY_NONE)
        cv_err("a TYPE member takes no type suffix; use AS");
    if (strchr(canon, '.') != NULL)
        cv_err("a TYPE member name cannot contain '.'");
    if (member_get(td, canon) != NULL)
        /* the firmware misses this check and the duplicate becomes
         * unreachable dead space - refuse it instead */
        cv_err("member '%s' declared twice in TYPE '%s'",
               t->text, td->disp);
    if (td->nmembers >= 16)
        cv_err("too many members in TYPE '%s' (16 is the "
               "firmware's limit)", td->disp);
    m = palloc(sizeof(*m));
    memset(m, 0, sizeof(*m));
    m->name = pstr(canon);
    m->disp = pstr(t->text);
    m->ty = TY_NONE;
    m->slen = 255;
    m->count = 1;
    if (accept_op("(")) {
        m->has_dims = 1;
        for (;;) {
            struct tok *d = nxt();
            if (d->kind != T_NUM || strcmp(d->up, "I") != 0)
                cv_err("a member array dimension must be a "
                       "literal integer");
            if (m->ndims >= MAXMDIMS)
                mm_error("line %d: too many member dimensions",
                         cv.lineno);
            m->dims[m->ndims++] = ll_digits(d->text);
            if (!accept_op(","))
                break;
        }
        expect_op(")");
    }
    if (!accept_kw("AS"))
        cv_err("invalid member definition in TYPE (missing AS)");
    w = nxt();
    if (w->kind != T_ID)
        cv_err("member type expected");
    if (strcmp(w->up, "INTEGER") == 0 || strcmp(w->up, "INT") == 0) {
        m->ty = TY_I;
    } else if (strcmp(w->up, "FLOAT") == 0) {
        m->ty = TY_F;
    } else if (strcmp(w->up, "STRING") == 0) {
        m->ty = TY_S;
        if (accept_kw("LENGTH")) {
            struct tok *n = nxt();
            long long ln;
            if (n->kind != T_NUM || strcmp(n->up, "I") != 0)
                cv_err("LENGTH takes a literal integer");
            ln = ll_digits(n->text);
            if (ln < 1 || ln > 255)
                cv_err("LENGTH must be 1..255");
            m->slen = ln;
        }
    } else {
        int wsfx;
        char *nc = split_suffix(w->text, &wsfx);
        if (types_get(nc) == NULL)
            cv_err("unknown type '%s' in TYPE definition (a "
                   "nested type must be defined earlier in the "
                   "file)", w->text);
        m->stype = pstr(nc);
    }
    td_add(td, m);
    return td;
}

/* One statement of the types pass.  Returns the open TypeDef, or NULL
 * outside a block. */
static struct typedef_rec *type_statement(struct typedef_rec *td)
{
    struct tok *t = peek(0);
    const char *up;

    if (t == NULL)
        return td;
    up = (t->kind == T_ID) ? t->up : t->text;
    if (td == NULL) {
        struct tok *p1 = peek(1), *p2 = peek(2);
        if (strcmp(up, "TYPE") == 0 && p1 != NULL && p1->kind == T_ID
            && (p2 == NULL
                || (p2->kind == T_OP && strcmp(p2->text, ":") == 0))) {
            struct tok *name;
            char *canon;
            int sfx;
            cv.i += 1;
            name = nxt();
            canon = split_suffix(name->text, &sfx);
            if (sfx != TY_NONE || strchr(canon, '.') != NULL)
                cv_err("invalid TYPE name '%s'", name->text);
            if (types_get(canon) != NULL)
                cv_err("TYPE '%s' already defined", name->text);
            if (cv.ntypes >= 32)
                cv_err("too many structure types (32 is the "
                       "firmware's limit)");
            td = palloc(sizeof(*td));
            memset(td, 0, sizeof(*td));
            td->name = pstr(canon);
            td->disp = pstr(name->text);
            td->where = cv.lineno;
            return td;
        }
        skip_statement();
        return NULL;
    }
    /* inside a block */
    if (strcmp(up, "END") == 0 && is_kw("TYPE", 1)) {
        cv.i += 2;
        if (td->nmembers == 0)
            cv_err("TYPE '%s' has no members", td->disp);
        td_close(td);
        GROW(cv.types, cv.ntypes, cv.ctypes);
        cv.types[cv.ntypes++] = td;
        return NULL;
    }
    if (strcmp(up, "TYPE") == 0)
        cv_err("nested TYPE is not allowed");
    return type_member(td);
}

/* Register every TYPE ... END TYPE before anything else needs one -
 * the firmware does the same in its pre-run scan, which is what lets
 * a DIM textually precede its TYPE.  Nested member types still
 * resolve in textual order, exactly as the firmware registers them. */
void pass_types(void)
{
    volatile int idx;
    struct typedef_rec *volatile td = NULL;

    cv.mode = M_TYPES;
    for (idx = 0; idx < src_nlines; idx++) {
        jmp_buf jb, *saved = err_jmp;
        struct tok *t;
        cv.lineno = idx + 1;
        err_jmp = &jb;
        if (setjmp(jb) != 0) {
            err_jmp = saved;
            continue;
        }
        cv.ntoks = tokenize(src_lines[idx], cv.lineno, cv.toks);
        err_jmp = saved;
        cv.i = 0;
        t = peek(0);
        if (t != NULL && t->kind == T_NUM && strcmp(t->up, "I") == 0)
            cv.i += 1;                       /* line number */
        while (!at_end()) {
            jmp_buf jb2, *saved2;
            if (accept_op(":"))
                continue;
            saved2 = err_jmp;
            err_jmp = &jb2;
            if (setjmp(jb2) == 0) {
                td = type_statement(td);
                err_jmp = saved2;
            } else {
                err_jmp = saved2;
                errors_add(err_msg);
                skip_statement();
            }
        }
    }
    if (td != NULL)
        errors_add(sfmt("line %d: TYPE '%s' has no matching END "
                        "TYPE", td->where, td->disp));
}

/* TYPE blocks are fully processed by pass_types; every later pass
 * just steps over them.  Returns 1 when the statement was part of a
 * block. */
int skip_type_block(const char *up)
{
    struct tok *p1, *p2;

    if (cv.in_type) {
        if (strcmp(up, "END") == 0 && is_kw("TYPE", 1)) {
            cv.i += 2;
            cv.in_type = 0;
        } else {
            skip_statement();
        }
        return 1;
    }
    p1 = peek(1);
    p2 = peek(2);
    if (strcmp(up, "TYPE") == 0 && p1 != NULL && p1->kind == T_ID
        && (p2 == NULL
            || (p2->kind == T_OP && strcmp(p2->text, ":") == 0))) {
        cv.i += 2;
        cv.in_type = 1;
        return 1;
    }
    return 0;
}

/* ---- assignment and the STRUCT statement ---- */

/* ... = expr  where the target is a structure member. */
void assign_member(struct mpres res)
{
    struct val v;

    expect_op("=");
    if (res.kind == MP_NUM) {
        v = expr();
        if (res.ty == TY_I)
            emit(sfmt("%s = %s;", res.code, as_int(v)));
        else
            emit(sfmt("%s = %s;", res.code, as_flt(v)));
        return;
    }
    if (res.kind == MP_STR) {
        v = expr();
        if (v.ty != TY_S)
            cv_err("cannot assign a number to a string member");
        /* bounded, and no trailing NUL when full: a member string is
         * LENGTH+1 bytes in the firmware's layout and the byte after
         * it belongs to the next member */
        emit(sfmt("mm_ssetm(%s, %lld, %s);", res.code, res.slen,
                  v.code));
        return;
    }
    /* a whole nested structure: the firmware memcpy's the OUTER
     * type's size here and overruns - refused, not reproduced */
    cv_err("assigning a whole structure into a member is not "
           "supported (the firmware overruns memory here); "
           "assign the member's own members instead");
}

/* target = <struct lvalue> - whole-structure copy. */
void assign_struct(const char *target, const char *tyname)
{
    struct val v = expr();

    if (v.ty != TY_T)
        cv_err("a structure can only be assigned a structure of "
               "the same TYPE");
    if (v.tm)
        cv_err("assigning a whole structure from a nested member "
               "is not supported (the firmware over-reads "
               "memory here)");
    if (strcmp(v.stype, tyname) != 0)
        cv_err("structure types must match (TYPE '%s' vs '%s')",
               tyname, v.stype);
    emit(sfmt("%s = %s;", target, v.code));
}

/* A STRUCT-verb operand: v, arr(i) or arr(). */
static struct sopnd struct_operand(void)
{
    struct tok *t = nxt();
    char *canon;
    int sfx;
    struct sym *s;
    struct sopnd r;

    if (t->kind != T_ID)
        cv_err("structure variable expected");
    canon = split_suffix(t->text, &sfx);
    if (strchr(canon, '.') != NULL)
        cv_err("STRUCT works on whole structures, not members");
    s = sym_lookup(canon);
    if (s == NULL || s->stype == NULL)
        cv_err("'%s' is not a structure variable", t->text);
    note_touch(canon, s);
    if (s->is_array) {
        const char *parts[MAXARGS];
        int np = 0, k;
        const char *code;

        expect_op("(");
        if (accept_op(")")) {
            r.all = 1;
            r.code = s->acc;
            r.s = s;
            return r;
        }
        parts[np++] = sfmt("(int)(%s)", as_int(expr()));
        while (accept_op(",")) {
            if (np >= MAXARGS)
                mm_error("line %d: too many subscripts", cv.lineno);
            parts[np++] = sfmt("(int)(%s)", as_int(expr()));
        }
        expect_op(")");
        if (np != s->ndims)
            cv_err("'%s' has %d dimension(s)", canon, s->ndims);
        code = s->acc;
        for (k = 0; k < np; k++)
            code = sfmt("%s[%s]", code, parts[k]);
        r.all = 0;
        r.code = code;
        r.s = s;
        return r;
    }
    r.all = 0;
    r.code = s->acc;
    r.s = s;
    return r;
}

/* STRUCT COPY|CLEAR|SWAP - the rest of the verbs need the
 * interpreter's machinery or a raw-file runtime entry and are refused
 * for now. */
void do_struct(void)
{
    struct tok *t = nxt();
    const char *verb = (t->kind == T_ID) ? t->up : "";

    if (strcmp(verb, "COPY") == 0) {
        struct sopnd src, dst;
        src = struct_operand();
        if (!accept_kw("TO"))
            cv_err("STRUCT COPY src TO dst");
        dst = struct_operand();
        if (strcmp(src.s->stype, dst.s->stype) != 0)
            cv_err("structure types must match");
        if (src.all != dst.all)
            cv_err("both operands must be arrays or both single "
                   "structures");
        if (src.all)
            emit(sfmt("memcpy(%s, %s, sizeof %s);",
                      dst.code, src.code, src.code));
        else
            emit(sfmt("%s = %s;", dst.code, src.code));
        return;
    }
    if (strcmp(verb, "CLEAR") == 0) {
        struct sopnd op = struct_operand();
        if (op.all)
            emit(sfmt("memset(%s, 0, sizeof %s);", op.code, op.code));
        else
            emit(sfmt("memset(&%s, 0, sizeof %s);", op.code, op.code));
        return;
    }
    if (strcmp(verb, "SWAP") == 0) {
        struct sopnd a, b;
        a = struct_operand();
        expect_op(",");
        b = struct_operand();
        if (strcmp(a.s->stype, b.s->stype) != 0)
            cv_err("structure types must match");
        if (a.all || b.all)
            cv_err("STRUCT SWAP takes single structures");
        /* no initialised declaration: the fcc front end takes struct
           assignment but not struct initialisers */
        emit(sfmt("{ struct t_%s __ts; __ts = %s; %s = %s; "
                  "%s = __ts; }",
                  a.s->stype, a.code, a.code, b.code, b.code));
        return;
    }
    if (strcmp(verb, "SORT") == 0 || strcmp(verb, "SAVE") == 0
        || strcmp(verb, "LOAD") == 0 || strcmp(verb, "PRINT") == 0
        || strcmp(verb, "EXTRACT") == 0 || strcmp(verb, "INSERT") == 0)
        cv_err("STRUCT %s is not translated yet", verb);
    cv_err("unknown STRUCT subcommand '%s'", t->text);
}

/* DIM v AS T = (v1, v2, ...) - values flattened in member order.
 * Emitted as ordinary member assignments; the firmware does not
 * length-check string values here, this does. */
void struct_initialiser(struct sym *s)
{
    struct typedef_rec *td;
    int k = 0;

    if (s != NULL && s->is_array)
        cv_err("an initialiser on a struct ARRAY is not "
               "translated yet");
    expect_op("(");
    td = (s != NULL) ? types_get(s->stype) : NULL;
    for (;;) {
        struct typemember *m;
        long long n, e;

        if (td == NULL || k >= td->nmembers)
            cv_err("too many initialisation values");
        m = td->members[k];
        k++;
        if (m->stype != NULL)
            cv_err("a nested-struct member cannot appear in an "
                   "initialiser (the firmware rejects it too)");
        n = m->count;
        for (e = 0; e < n; e++) {
            struct val v = expr();
            if (cv.mode == M_EMIT) {
                const char *code = sfmt("%s.m_%s", s->acc, m->name);
                if (m->ty == TY_S) {
                    if (m->has_dims)
                        code = sfmt("(%s + %lld)", code,
                                    e * (m->slen + 1));
                    emit(sfmt("mm_ssetm(%s, %lld, %s);",
                              code, m->slen, as_str(v)));
                } else {
                    if (m->has_dims)
                        code = sfmt("%s[%lld]", code, e);
                    emit(sfmt("%s = %s;", code,
                              m->ty == TY_I ? as_int(v) : as_flt(v)));
                }
            }
            if (e < n - 1)
                expect_op(",");
        }
        if (!accept_op(","))
            break;
    }
    expect_op(")");
    if (td != NULL && k < td->nmembers)
        cv_err("not enough initialisation values for TYPE '%s'",
               td->disp);
}

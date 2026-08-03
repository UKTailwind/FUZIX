/* mmbc_stmt.c - the statement region: statement_inner and every do_*
 * handler, plus the SUB/FUNCTION open/close machinery.
 *
 * Mirrors mmb2c.py statement_inner (1911) through close_routine (3205),
 * function for function in the Python's order.  Error and warning text
 * is part of byte identity (skip reasons land in the generated C's
 * header comment) - keep every string exact. */

#include "mmbc.h"
#include "mmbc_expr.h"

/* ---- forward declarations, Python order ---- */

static void do_print(void);
static char *prcall(const char *chan, const char *what, const char *arg);
static void do_longstring(void);
static const char *gosub_key(void);
static void do_gosub(void);
static void emit_gosub(const char *canon, const char *disp);
static void do_return(void);
static void do_read(void);
static void do_restore(void);
static void do_sort(void);
static void do_inc(void);
static void do_cat(void);
static void do_erase(void);
static void do_on_goto(void);
static void do_array_cmd(void);
static void do_open(void);
static void do_close(void);
static void do_fileword(const char *up);
static struct val input_target(void);
static void do_input(void);
static void do_line_input(void);
static int looks_like_assignment(void);
static void do_assign_or_call(void);
static void do_callstmt(void);
static void do_mid_assign(void);
static const char *lvalue_from_here(void);
static void do_assign(void);
static char *cond(void);
static void do_if(void);
static void inline_statements(void);
static void do_elseif(void);
static void do_else(void);
static void close_block(const char *kind);
static void do_for(void);
static void do_next(void);
static void do_do(void);
static void do_loop(void);
static void do_while(void);
static void do_select(void);
static void do_case(void);
static const char *case_test(const char *name, int ty);
static void do_exit(void);
static void note_goto(const char *canon);
static void do_goto(void);
static void do_end(void);
static void open_routine(int is_func);
static void emit_local_decl(struct sym *s);

/*
 * What has to run on every path OUT of a routine.
 *
 * Not the same thing as the release emitted after a statement or round
 * a loop condition: those wind the scratch pools back to __mark and
 * know nothing about the local block, which is not in them.  Only
 * leaving the routine gives the block back, so every path out has to
 * say so - and mm_error exits the process rather than unwinding, so
 * those are all of them.
 */
static const char *routine_exit(void)
{
    if (cv.cur != NULL && cv.cur->heap_locals)
        return "mm_lfree(__L); mm_release(__mark);";
    return "mm_release(__mark);";
}
static void close_routine(int is_func);

/* ---- local helpers ---- */

/* pfx + name.replace('.', '__') */
static char *dunder(const char *pfx, const char *name)
{
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

static struct sym *locals_get(struct routine *r, const char *nm)
{
    int k;
    for (k = 0; k < r->nlocals; k++)
        if (strcmp(r->locals[k]->name, nm) == 0)
            return r->locals[k];
    return NULL;
}

/* gosub_sites: find only (dict .get) */
static struct gsub *gsub_find(const char *routine)
{
    int k;
    for (k = 0; k < cv.ngsubs; k++)
        if (strcmp(cv.gsubs[k].routine, routine) == 0)
            return &cv.gsubs[k];
    return NULL;
}

/* gosub_sites: find-or-create (dict .setdefault) */
static struct gsub *gsub_rec(const char *routine)
{
    struct gsub *g = gsub_find(routine);

    if (g != NULL)
        return g;
    GROW(cv.gsubs, cv.ngsubs, cv.cgsubs);
    g = &cv.gsubs[cv.ngsubs++];
    g->routine = pstr(routine);
    g->sites = NULL;
    g->n = 0;
    g->cap = 0;
    return g;
}

/* Push a block tuple; a must be pstr()'d by the caller when it is not
 * a string literal (blocks outlive the line). */
static void push_block(const char *kind, const char *a, int ty)
{
    struct block *b;

    GROW(cv.blocks, cv.nblocks, cv.cblocks);
    b = &cv.blocks[cv.nblocks++];
    b->kind = kind;
    b->a = a;
    b->b = NULL;
    b->c = NULL;
    b->d = NULL;
    b->ty = ty;
    b->line = cv.lineno;
}

/* ---- the dispatcher ---- */

void statement_inner(void)
{
    struct tok *t = peek(0);
    const char *up;

    if (t == NULL)
        return;
    up = (t->kind == T_ID) ? t->up : t->text;

    if (strcmp(up, "OPTION") == 0) {
        cv.i++;
        do_option();
        skip_statement();
        return;
    }
    if (strcmp(up, "DIM") == 0 || strcmp(up, "LOCAL") == 0
        || strcmp(up, "STATIC") == 0 || strcmp(up, "CONST") == 0) {
        cv.i++;
        do_declare(up);
        return;
    }
    if (strcmp(up, "PRINT") == 0 || strcmp(up, "?") == 0) {
        cv.i++;
        do_print();
        return;
    }
    if (strcmp(up, "LET") == 0) {
        cv.i++;
        do_assign();
        return;
    }
    if (strcmp(up, "IF") == 0) {
        cv.i++;
        do_if();
        return;
    }
    if (strcmp(up, "ELSEIF") == 0) {
        cv.i++;
        do_elseif();
        return;
    }
    if (strcmp(up, "ELSE") == 0) {
        cv.i++;
        do_else();
        return;
    }
    if (strcmp(up, "ENDIF") == 0) {
        cv.i++;
        close_block("if");
        return;
    }
    if (strcmp(up, "FOR") == 0) {
        cv.i++;
        do_for();
        return;
    }
    if (strcmp(up, "NEXT") == 0) {
        cv.i++;
        do_next();
        return;
    }
    if (strcmp(up, "DO") == 0) {
        cv.i++;
        do_do();
        return;
    }
    if (strcmp(up, "LOOP") == 0) {
        cv.i++;
        do_loop();
        return;
    }
    if (strcmp(up, "WHILE") == 0) {
        cv.i++;
        do_while();
        return;
    }
    if (strcmp(up, "WEND") == 0) {
        cv.i++;
        close_block("while");
        return;
    }
    if (strcmp(up, "SELECT") == 0) {
        cv.i++;
        do_select();
        return;
    }
    if (strcmp(up, "CASE") == 0) {
        cv.i++;
        do_case();
        return;
    }
    if (strcmp(up, "EXIT") == 0) {
        cv.i++;
        do_exit();
        return;
    }
    if (strcmp(up, "GOTO") == 0) {
        cv.i++;
        do_goto();
        return;
    }
    if (strcmp(up, "SUB") == 0 || strcmp(up, "FUNCTION") == 0) {
        cv.i++;
        open_routine(strcmp(up, "FUNCTION") == 0);
        return;
    }
    if (strcmp(up, "END") == 0) {
        cv.i++;
        do_end();
        return;
    }
    if (strcmp(up, "CALL") == 0) {
        cv.i++;
        do_callstmt();
        return;
    }
    if (strcmp(up, "OPEN") == 0) {
        cv.i++;
        do_open();
        return;
    }
    if (strcmp(up, "CLOSE") == 0) {
        cv.i++;
        do_close();
        return;
    }
    if (strcmp(up, "INPUT") == 0) {
        cv.i++;
        do_input();
        return;
    }
    if (strcmp(up, "LINE") == 0 && is_kw("INPUT", 1)) {
        cv.i += 2;
        do_line_input();
        return;
    }
    if (strcmp(up, "SEEK") == 0) {
        const char *fn;
        struct val pos;
        cv.i++;
        fn = channel();
        expect_op(",");
        pos = expr();
        emit(sfmt("mm_seek(%s, %s);", fn, as_int(pos)));
        return;
    }
    if (strcmp(up, "KILL") == 0 || strcmp(up, "MKDIR") == 0
        || strcmp(up, "RMDIR") == 0 || strcmp(up, "CHDIR") == 0
        || strcmp(up, "FILES") == 0) {
        cv.i++;
        do_fileword(up);
        return;
    }
    if (strcmp(up, "RENAME") == 0 || strcmp(up, "COPY") == 0) {
        struct val a, b;
        cv.i++;
        a = expr();
        if (a.ty != TY_S)
            cv_err("%s needs a file name string", up);
        if (strcmp(up, "RENAME") == 0) {
            if (!accept_kw("AS"))
                cv_err("RENAME old$ AS new$");
        } else {
            if (!accept_kw("TO"))
                cv_err("COPY from$ TO to$");
        }
        b = expr();
        if (b.ty != TY_S)
            cv_err("%s needs a file name string", up);
        emit(sfmt("mm_%s(%s, %s);",
                  strcmp(up, "RENAME") == 0 ? "rename" : "copy",
                  a.code, b.code));
        return;
    }
    if (strcmp(up, "DATA") == 0) {
        cv.i++;
        skip_statement();          /* gathered in the decl pass */
        return;
    }
    if (strcmp(up, "READ") == 0) {
        cv.i++;
        do_read();
        return;
    }
    if (strcmp(up, "RESTORE") == 0) {
        cv.i++;
        do_restore();
        return;
    }
    if (strcmp(up, "SORT") == 0) {
        cv.i++;
        do_sort();
        return;
    }
    if (strcmp(up, "CONTINUE") == 0) {
        cv.i++;
        if (accept_kw("FOR") || accept_kw("DO")) {
            emit("continue;");
            return;
        }
        cv_err("only CONTINUE FOR and CONTINUE DO can be translated");
    }
    if (strcmp(up, "INC") == 0) {
        cv.i++;
        do_inc();
        return;
    }
    if (strcmp(up, "CAT") == 0) {
        cv.i++;
        do_cat();
        return;
    }
    if (strcmp(up, "ERASE") == 0) {
        cv.i++;
        do_erase();
        return;
    }
    if (strcmp(up, "CLEAR") == 0) {
        cv.i++;
        cv_warn("CLEAR zeroes every global; static storage cannot be "
                "handed back the way the interpreter does");
        emit("__mmb_clear();");
        cv.uses_clear = 1;
        return;
    }
    if (strcmp(up, "CLS") == 0) {
        cv.i++;
        emit("mm_cls();");
        return;
    }
    if (strcmp(up, "MODE") == 0) {
        /* MODE 1  640x480, one bit    MODE 2  320x240, 16 colours
           The PicoMite VGA numbering, which is also the first two
           HDMI modes; the runtime maps it onto the kernel's own. */
        struct val n;
        cv.i++;
        n = expr();
        emit(sfmt("mm_mode(%s);", as_int(n)));
        return;
    }
    if (strcmp(up, "FRAMEBUFFER") == 0) {
        /* FRAMEBUFFER CREATE | CLOSE [F] | WRITE N|F |
                       COPY s, d [, B] | WAIT

           MMBasic's Draw.c cmd_framebuffer, reduced to the "F" buffer:
           draw off-screen, then show it in one COPY.  The machine has
           one off-screen buffer and no transparent blit, so LAYER and
           MERGE are refused by name below rather than translated into
           something that is not them.

           A mode change discards the buffer, both here and in the
           kernel, so CREATE belongs after MODE - which is also where
           MMBasic wants it, setmode() closing every buffer. */
        cv.i++;
        if (accept_kw("CREATE")) {
            emit("mm_fb_create();");
            return;
        }
        if (accept_kw("CLOSE")) {
            accept_kw("F");             /* the only one there is to close */
            emit("mm_fb_close();");
            return;
        }
        if (accept_kw("WRITE")) {
            emit(sfmt("mm_fb_write(%d);", fb_buf()));
            return;
        }
        if (accept_kw("COPY")) {
            int s, d, b = 0;
            s = fb_buf();
            expect_op(",");
            d = fb_buf();
            if (accept_op(",")) {
                if (!accept_kw("B"))
                    cv_err("FRAMEBUFFER COPY takes only B here");
                b = 1;
            }
            emit(sfmt("mm_fb_copy(%d, %d, %d);", s, d, b));
            return;
        }
        if (accept_kw("WAIT")) {
            emit("mm_fb_wait();");
            return;
        }
        cv_err("only FRAMEBUFFER CREATE, CLOSE, WRITE, COPY and "
               "WAIT are translated");
    }
    if (strcmp(up, "SYSTEM") == 0
        || ((strcmp(up, "SAVE") == 0 || strcmp(up, "LOAD") == 0)
            && is_kw("IMAGE", 1))) {
        /* SYSTEM prog$ [, arg ...]        run a program and wait
           SAVE IMAGE f$ [, x, y, w, h]    both are programs
           LOAD IMAGE f$ [, x, y]

           An argv, not a command line: nothing to quote and no shell
           in the middle.  MMBasic has no SYSTEM - it is firmware with
           nothing to run - so that spelling is ours, but SAVE IMAGE
           and LOAD IMAGE are the interpreter's own and are simply
           handed to /usr/bin/saveimage and /usr/bin/loadimage. */
        const char *prog = NULL;
        int first = 1;

        if (strcmp(up, "SYSTEM") == 0) {
            cv.i++;
        } else {
            cv.i += 2;
            prog = (strcmp(up, "SAVE") == 0) ? "saveimage" : "loadimage";
        }
        emit("mm_run_begin();");
        if (prog != NULL)
            emit(sfmt("mm_run_arg(%s);", c_string_literal(prog)));
        for (;;) {
            struct val v;
            if (!first && !accept_op(","))
                break;
            v = expr();
            if (v.ty == TY_S)
                emit(sfmt("mm_run_arg(%s);", v.code));
            else if (v.ty == TY_I)
                emit(sfmt("mm_run_arg_i(%s);", v.code));
            else
                emit(sfmt("mm_run_arg_f(%s);", v.code));
            first = 0;
        }
        emit("mm_run_exec();");
        return;
    }
    if (strcmp(up, "CIRCLE") == 0) {
        /* CIRCLE x, y, r [, lw [, aspect [, colour [, fill]]]]
           The geometry is mmb_gfx.h's, not the runtime's.  MMBasic
           treats an omitted argument as the default, so a bare comma
           is legal in every position. */
        const char *x, *y, *r;
        const char *lw = "1LL", *asp = "1.0";
        const char *col = "MM_CUR", *fill = "MM_CUR";

        cv.i++;
        x = as_int(expr());
        expect_op(",");
        y = as_int(expr());
        expect_op(",");
        r = as_int(expr());
        if (accept_op(",")) {
            if (!is_op(",", 0))
                lw = as_int(expr());
            if (accept_op(",")) {
                if (!is_op(",", 0))
                    asp = as_flt(expr());
                if (accept_op(",")) {
                    if (!is_op(",", 0))
                        col = as_int(expr());
                    if (accept_op(","))
                        fill = as_int(expr());
                }
            }
        }
        cv.uses_gfx = 1;
        emit(sfmt("mmg_circle(%s, %s, %s, %s, %s, %s, %s);",
                  x, y, r, lw, col, fill, asp));
        return;
    }
    if (strcmp(up, "COLOUR") == 0 || strcmp(up, "COLOR") == 0) {
        /* COLOUR fg [, bg].  Everything that draws without being given
           a colour uses fg.  bg is remembered but nothing reads it yet
           - TEXT and the filled shapes will. */
        struct val fg;
        const char *bg;
        const char *f;

        cv.i++;
        fg = expr();
        if (accept_op(","))
            bg = as_int(expr());
        else
            bg = "MM_CUR";
        f = as_int(fg);
        emit(sfmt("mm_colour(%s, %s);", f, bg));
        return;
    }
    if (strcmp(up, "PIXEL") == 0) {
        /* PIXEL x, y        - in the current foreground colour
           PIXEL x, y, c     - c is RGB888, as everywhere in MMBasic;
                               the kernel primitive converts it to
                               whatever the current mode uses.
           The function form PIXEL(x,y) is handled in the expression
           parser; a statement never starts with the open bracket. */
        struct val x, y;
        const char *col;
        const char *xs, *ys;

        cv.i++;
        x = expr();
        expect_op(",");
        y = expr();
        if (is_op(",", 0)) {
            struct val c;
            cv.i++;
            c = expr();
            col = as_int(c);
        } else {
            col = "MM_CUR";
        }
        xs = as_int(x);
        ys = as_int(y);
        emit(sfmt("mm_pixel(%s, %s, %s);", xs, ys, col));
        return;
    }
    if (strcmp(up, "LINE") == 0) {
        /* LINE x1, y1, x2, y2 [, width [, colour]]
         *
         * The geometry is in the runtime, not the kernel: an
         * axis-aligned line becomes one span and the rest is Bresenham
         * into a batch, so the whole line crosses into the kernel once.
         * Measured 433us point-by-point against 71us batched for a 312
         * point diagonal. */
        struct val x1, y1, x2, y2;
        const char *col = "MM_CUR";
        const char *a, *b, *c2, *d;

        cv.i++;
        x1 = expr();
        expect_op(",");
        y1 = expr();
        expect_op(",");
        x2 = expr();
        expect_op(",");
        y2 = expr();
        if (accept_op(",")) {
            /* x1..y2 are required; the optional ones after them may
               each be left blank, so LINE x1,y1,x2,y2,,c is how a
               colour is given without a width. */
            if (!is_op(",", 0)) {
                struct val w = expr();
                if (strcmp(w.code, "1LL") != 0 && strcmp(w.code, "1") != 0)
                    cv_warn("LINE width is not supported yet; drawn 1 pixel wide");
            }
            if (accept_op(","))
                col = as_int(expr());
        }
        a = as_int(x1);
        b = as_int(y1);
        c2 = as_int(x2);
        d = as_int(y2);
        emit(sfmt("mm_line(%s, %s, %s, %s, %s);", a, b, c2, d, col));
        return;
    }
    if (strcmp(up, "PAUSE") == 0) {
        struct val v;
        cv.i++;
        v = expr();
        emit(sfmt("mm_pause(%s);", as_flt(v)));
        return;
    }
    if (strcmp(up, "ERROR") == 0) {
        cv.i++;
        if (stmt_end()) {
            emit("mm_error(\"Program halted by ERROR\");");
        } else {
            struct val v = expr();
            if (v.ty != TY_S)
                cv_err("ERROR needs a message string");
            emit(sfmt("mm_error_s(%s);", v.code));
        }
        return;
    }
    if (strcmp(up, "ON") == 0 && is_kw("ERROR", 1)) {
        struct tok *w;
        const char *kw;
        cv.i += 2;
        w = peek(0);
        kw = (w != NULL && w->kind == T_ID) ? w->up : "";
        skip_statement();
        if (strcmp(kw, "IGNORE") == 0 || strcmp(kw, "SKIP") == 0
            || strcmp(kw, "RESTART") == 0)
            cv_err("ON ERROR %s needs soft error handling, which is "
                   "not in yet; only ABORT and CLEAR are translated",
                   kw);
        return;
    }
    if (strcmp(up, "ON") == 0 && peek(1) != NULL
        && !is_kw("ERROR", 1) && !is_kw("KEY", 1) && !is_kw("PS2", 1)) {
        cv.i++;
        do_on_goto();
        return;
    }
    if (strcmp(up, "ARRAY") == 0) {
        cv.i++;
        do_array_cmd();
        return;
    }
    if (strcmp(up, "MATH") == 0 && !is_op("(", 1)) {
        cv.i++;
        do_array_cmd();
        return;
    }
    if ((strcmp(up, "TIMER") == 0 || strcmp(up, "DATE$") == 0
         || strcmp(up, "TIME$") == 0) && is_op("=", 1)) {
        struct val v;
        cv.i += 2;
        v = expr();
        if (strcmp(up, "TIMER") == 0)
            emit(sfmt("mm_timer_set(%s);", as_flt(v)));
        else if (v.ty != TY_S)
            cv_err("%s = needs a string", up);
        else
            emit(sfmt("mm_set_%s(%s);",
                      strcmp(up, "DATE$") == 0 ? "date" : "time",
                      v.code));
        return;
    }
    if (strcmp(up, "LONGSTRING") == 0) {
        cv.i++;
        do_longstring();
        return;
    }
    if (strcmp(up, "GOSUB") == 0) {
        cv.i++;
        do_gosub();
        return;
    }
    if (strcmp(up, "RETURN") == 0) {
        cv.i++;
        do_return();
        return;
    }
    if (strcmp(up, "RANDOMIZE") == 0) {
        struct val v;
        cv.i++;
        v = expr();
        emit(sfmt("mm_randomize(%s);", as_int(v)));
        return;
    }
    if (t->kind == T_ID) {
        do_assign_or_call();
        return;
    }
    cv_err("cannot parse statement starting with '%s'", t->text);
}

/* -- PRINT ----------------------------------------------------------- */

static void do_print(void)
{
    const char *chan = NULL;
    int suppress_nl = 0;

    if (is_op("#", 0)) {
        chan = channel();
        accept_op(",");          /* the comma after #n is not a tab */
    }
    while (!stmt_end()) {
        struct val v;
        if (accept_op(";")) {
            suppress_nl = 1;
            continue;
        }
        if (accept_op(",")) {
            emit(prcall(chan, "tab", NULL));
            suppress_nl = 1;
            continue;
        }
        v = expr();
        suppress_nl = 0;
        if (v.ty == TY_S)
            emit(prcall(chan, "s", v.code));
        else if (v.ty == TY_I)
            emit(prcall(chan, "i", v.code));
        else
            emit(prcall(chan, "f", v.code));
    }
    if (!suppress_nl)
        emit(prcall(chan, "nl", NULL));
}

static char *prcall(const char *chan, const char *what, const char *arg)
{
    if (chan == NULL)
        return sfmt("mm_pr_%s(%s);", what, arg != NULL ? arg : "");
    if (arg == NULL)
        return sfmt("mm_fpr_%s(%s);", what, chan);
    return sfmt("mm_fpr_%s(%s, %s);", what, chan, arg);
}

/* -- LONGSTRING ------------------------------------------------------ */

static void do_longstring(void)
{
    struct tok *t = nxt();
    const char *op;

    if (t->kind != T_ID)
        cv_err("LONGSTRING needs a sub-command");
    op = t->up;

    if (strcmp(op, "CLEAR") == 0 || strcmp(op, "UCASE") == 0
        || strcmp(op, "LCASE") == 0) {
        struct flat f = lsref();
        if (strcmp(op, "CLEAR") == 0)
            emit(sfmt("mm_ls_clear(%s, %s);", f.ptr, f.cnt));
        else
            emit(sfmt("mm_ls_%s(%s);", lower(op), f.ptr));
        return;
    }

    if (strcmp(op, "APPEND") == 0 || strcmp(op, "REPLACE") == 0) {
        struct flat f = lsref();
        struct val v, st;
        expect_op(",");
        v = expr();
        if (v.ty != TY_S)
            cv_err("LONGSTRING %s needs a normal string", op);
        if (strcmp(op, "APPEND") == 0) {
            emit(sfmt("mm_ls_append(%s, %s, %s);", f.ptr, f.cnt,
                      v.code));
            return;
        }
        expect_op(",");
        st = expr();
        emit(sfmt("mm_ls_replace(%s, %s, %s, %s);",
                  f.ptr, f.cnt, v.code, as_int(st)));
        return;
    }

    if (strcmp(op, "LOAD") == 0) {
        struct flat f = lsref();
        struct val n, v;
        expect_op(",");
        n = expr();
        expect_op(",");
        v = expr();
        if (v.ty != TY_S)
            cv_err("LONGSTRING LOAD needs a normal string");
        emit(sfmt("mm_ls_load(%s, %s, %s, %s);",
                  f.ptr, f.cnt, as_int(n), v.code));
        return;
    }

    if (strcmp(op, "COPY") == 0 || strcmp(op, "CONCAT") == 0) {
        struct flat d = lsref();
        struct flat s;
        expect_op(",");
        s = lsref();
        emit(sfmt("mm_ls_%s(%s, %s, %s);",
                  lower(op), d.ptr, d.cnt, s.ptr));
        return;
    }

    if (strcmp(op, "LEFT") == 0 || strcmp(op, "RIGHT") == 0
        || strcmp(op, "MID") == 0) {
        struct flat d = lsref();
        struct flat s;
        struct val a;
        expect_op(",");
        s = lsref();
        expect_op(",");
        a = expr();
        if (strcmp(op, "MID") == 0) {
            const char *b = "-1";
            if (accept_op(","))
                b = as_int(expr());
            emit(sfmt("mm_ls_mid(%s, %s, %s, %s, %s);",
                      d.ptr, d.cnt, s.ptr, as_int(a), b));
            return;
        }
        emit(sfmt("mm_ls_%s(%s, %s, %s, %s);",
                  lower(op), d.ptr, d.cnt, s.ptr, as_int(a)));
        return;
    }

    if (strcmp(op, "RESIZE") == 0 || strcmp(op, "TRIM") == 0) {
        struct flat f = lsref();
        struct val n;
        expect_op(",");
        n = expr();
        emit(sfmt("mm_ls_%s(%s, %s, %s);",
                  lower(op), f.ptr, f.cnt, as_int(n)));
        return;
    }

    if (strcmp(op, "SETBYTE") == 0) {
        struct flat f = lsref();
        struct val n, v;
        expect_op(",");
        n = expr();
        expect_op(",");
        v = expr();
        emit(sfmt("mm_ls_setbyte(%s, %s, (%s) - %d, %s);",
                  f.ptr, f.cnt, as_int(n), cv.opt_base, as_int(v)));
        return;
    }

    if (strcmp(op, "PRINT") == 0) {
        const char *chan = "0";
        struct flat f;
        const char *nl = "1";
        if (is_op("#", 0)) {
            chan = channel();
            accept_op(",");
        }
        f = lsref();
        if (accept_op(";") || accept_op(","))
            nl = "0";
        emit(sfmt("mm_ls_print(%s, %s, %s);", chan, f.ptr, nl));
        return;
    }

    cv_err("LONGSTRING %s is not supported", t->text);
}

/* -- GOSUB / RETURN -------------------------------------------------- */

static const char *gosub_key(void)
{
    return cv.cur ? cv.cur->name : "";
}

static void do_gosub(void)
{
    struct tok *t = nxt();
    const char *canon = NULL;
    int sfx;

    if (t->kind == T_NUM && strcmp(t->up, "I") == 0)
        canon = t->text;
    else if (t->kind == T_ID)
        canon = split_suffix(t->text, &sfx);
    else
        cv_err("GOSUB needs a label or line number");
    emit_gosub(canon, t->text);
}

static void emit_gosub(const char *canon, const char *disp)
{
    struct label *l = label_rec(canon);
    const char *here, *there;
    int site;

    if (!l->placed)
        cv_err("unknown label '%s'", disp);
    here = gosub_key();
    there = l->routine ? l->routine : "";
    if (strcmp(here, there) != 0)
        cv_err("GOSUB '%s' crosses a SUB/FUNCTION boundary; C cannot "
               "jump between functions, so move the target or use a "
               "SUB", disp);
    cv.gosub_n++;
    site = cv.gosub_n;
    l->used = 1;
    note_goto(canon);
    if (cv.mode == M_SCAN) {
        struct gsub *g = gsub_rec(here);
        GROW(g->sites, g->n, g->cap);
        g->sites[g->n++] = site;
    }
    emit(sfmt("mm_gosub_push(%d); goto %s;", site, clabel(canon)));
    raw(sfmt("__GR%d: ;", site));
}

static void do_return(void)
{
    struct gsub *g = gsub_find(gosub_key());
    int k;

    if (g == NULL || g->n == 0)
        cv_err("RETURN without any GOSUB in this part of the program");
    emit("switch (mm_gosub_pop()) {");
    for (k = 0; k < g->n; k++)
        emit(sfmt("    case %d: goto __GR%d;", g->sites[k],
                  g->sites[k]));
    emit("    default: mm_error(\"RETURN without GOSUB\");");
    emit("}");
}

/* -- DATA / READ / RESTORE ------------------------------------------- */

static void do_read(void)
{
    if (accept_kw("SAVE")) {
        emit("mm_read_save();");
        return;
    }
    if (accept_kw("RESTORE")) {
        emit("mm_read_unsave();");
        return;
    }
    while (!stmt_end()) {
        struct tok *t = peek(0);
        if (t != NULL && t->kind == T_ID && is_op("(", 1)
            && is_op(")", 2)) {
            struct sym *sym = arrayref(1);
            struct flat f = array_flat(sym);
            char *k = newtmp("k");
            emit(sfmt("{ int %s; for (%s = 0; %s < %s; %s++)",
                      k, k, k, f.cnt, k));
            if (sym->ty == TY_S)
                emit(sfmt("    mm_sset((%s)[%s], mm_read_s()); }",
                          f.ptr, k));
            else
                emit(sfmt("    (%s)[%s] = mm_read_%s(); }",
                          f.ptr, k, sym->ty == TY_I ? "i" : "f"));
            cv.tmp_used = 1;
        } else {
            struct val tgt = input_target();
            if (tgt.ty == TY_S) {
                emit(sfmt("mm_sset(%s, mm_read_s());", tgt.code));
                cv.tmp_used = 1;
            } else {
                emit(sfmt("%s = mm_read_%s();", tgt.code,
                          tgt.ty == TY_I ? "i" : "f"));
            }
        }
        if (!accept_op(","))
            break;
    }
}

static void do_restore(void)
{
    struct tok *t;
    const char *canon = NULL;
    struct label *l;
    int sfx;

    if (stmt_end()) {
        emit("mm_restore(0);");
        return;
    }
    t = nxt();
    if (t->kind == T_NUM && strcmp(t->up, "I") == 0)
        canon = t->text;
    else if (t->kind == T_ID)
        canon = split_suffix(t->text, &sfx);
    else
        cv_err("RESTORE needs a label or line number");
    l = label_rec(canon);
    if (!l->has_data_at)
        cv_err("unknown label '%s' in RESTORE", t->text);
    emit(sfmt("mm_restore(%d);", l->data_at));
}

/* -- SORT ------------------------------------------------------------ */

static void do_sort(void)
{
    struct sym *sym = arrayref(1);
    struct flat f = array_flat(sym);
    const char *idx = "NULL";
    const char *flags = "0";
    const char *start = sfmt("%d", cv.opt_base);
    const char *count = "-1";
    const char *kind;

    if (accept_op(",")) {
        if (!(is_op(",", 0) || stmt_end())) {
            struct sym *isym = arrayref(1);
            if (isym->ty != TY_I)
                cv_err("the SORT index array must be an integer array");
            idx = array_flat(isym).ptr;
        }
        if (accept_op(",")) {
            if (!(is_op(",", 0) || stmt_end()))
                flags = as_int(expr());
            if (accept_op(",")) {
                if (!(is_op(",", 0) || stmt_end()))
                    start = as_int(expr());
                if (accept_op(",")) {
                    if (!stmt_end())
                        count = as_int(expr());
                }
            }
        }
    }
    kind = sym->ty == TY_I ? "i" : sym->ty == TY_F ? "f" : "s";
    emit(sfmt("mm_sort_%s(%s, %s, %s, (int)(%s), (int)(%s), (int)(%s));",
              kind, f.ptr, idx, f.cnt, start, count, flags));
}

/* -- INC / CAT / ERASE ----------------------------------------------- */

static void do_inc(void)
{
    struct val tgt = input_target();
    struct val v;

    if (accept_op(",")) {
        v = expr();
    } else {
        v.code = "1LL";
        v.ty = TY_I;
    }
    if (tgt.ty == TY_S) {
        if (v.ty != TY_S)
            cv_err("INC on a string needs a string increment");
        emit(sfmt("mm_sset(%s, mm_scat(%s, %s));", tgt.code, tgt.code,
                  v.code));
        cv.tmp_used = 1;
    } else if (tgt.ty == TY_I) {
        emit(sfmt("%s += %s;", tgt.code, as_int(v)));
    } else {
        emit(sfmt("%s += %s;", tgt.code, as_flt(v)));
    }
}

static void do_cat(void)
{
    struct val tgt = input_target();
    struct val v;

    if (tgt.ty != TY_S)
        cv_err("CAT needs a string variable");
    expect_op(",");
    v = expr();
    if (v.ty != TY_S)
        cv_err("CAT needs a string to append");
    emit(sfmt("mm_sset(%s, mm_scat(%s, %s));", tgt.code, tgt.code,
              v.code));
    cv.tmp_used = 1;
}

static void do_erase(void)
{
    cv_warn("ERASE zeroes the variable; static storage cannot be "
            "handed back the way the interpreter does");
    while (!stmt_end()) {
        struct tok *t = peek(0);
        struct sym *sym;
        if (t == NULL || t->kind != T_ID)
            cv_err("ERASE needs a variable name");
        sym = reference(t->text, 0);
        cv.i++;
        if (accept_op("("))
            expect_op(")");
        emit(zero_of(sym));
        if (!accept_op(","))
            break;
    }
}

char *zero_of(struct sym *sym)
{
    if (sym->is_array) {
        struct flat f = array_flat(sym);
        if (sym->ty == TY_S)
            return sfmt("mm_arr_set_s(%s, %s, \"\\000\" \"\");",
                        f.ptr, f.cnt);
        return sfmt("mm_arr_set_%s(%s, %s, 0);",
                    sym->ty == TY_I ? "i" : "f", f.ptr, f.cnt);
    }
    if (sym->ty == TY_S)
        return sfmt("%s[0] = 0; %s[1] = 0;", sym->acc, sym->acc);
    return sfmt("%s = 0;", sym->acc);
}

/* -- ON nbr GOTO ----------------------------------------------------- */

#define MAXTARGETS 128

static void do_on_goto(void)
{
    struct val v = expr();
    int is_gosub = 0;
    const char *targets[MAXTARGETS];
    int ntargets = 0;
    int k;

    if (accept_kw("GOSUB"))
        is_gosub = 1;
    else if (!accept_kw("GOTO"))
        cv_err("ON <expr> GOTO|GOSUB label, label, ...");
    for (;;) {
        struct tok *t = nxt();
        const char *canon = NULL;
        struct label *l;
        int sfx;
        if (t->kind == T_NUM && strcmp(t->up, "I") == 0)
            canon = t->text;
        else if (t->kind == T_ID)
            canon = split_suffix(t->text, &sfx);
        else
            cv_err("ON ... GOTO needs labels");
        l = label_rec(canon);
        if (!l->placed)
            cv_err("unknown label '%s'", t->text);
        l->used = 1;
        note_goto(canon);
        if (ntargets >= MAXTARGETS)
            mm_error("line %d: too many ON GOTO targets", cv.lineno);
        targets[ntargets++] = canon;
        if (!accept_op(","))
            break;
    }
    if (is_gosub) {
        /* each arm is its own GOSUB site so that RETURN lands
         * correctly */
        char *sel = newtmp("on");
        emit(sfmt("{ int %s = (int)(%s);", sel, as_int(v)));
        cv.indent++;
        for (k = 0; k < ntargets; k++) {
            emit(sfmt("if (%s == %d) {", sel, k + 1));
            cv.indent++;
            emit_gosub(targets[k], targets[k]);
            cv.indent--;
            emit("}");
        }
        cv.indent--;
        emit("}");
        return;
    }
    emit(sfmt("switch ((int)(%s)) {", as_int(v)));
    for (k = 0; k < ntargets; k++)
        emit(sfmt("    case %d: goto %s;", k + 1, clabel(targets[k])));
    emit("    default: break;");
    emit("}");
}

/* -- ARRAY / MATH whole array commands ------------------------------- */

static void do_array_cmd(void)
{
    struct tok *t = nxt();
    const char *op;

    if (t->kind != T_ID)
        cv_err("ARRAY/MATH needs a sub-command");
    op = t->up;
    if (strcmp(op, "SET") == 0) {
        struct val val = expr();
        struct sym *sym;
        struct flat f;
        expect_op(",");
        sym = arrayref(1);
        f = array_flat(sym);
        if (sym->ty == TY_S) {
            if (val.ty != TY_S)
                cv_err("a string array needs a string value");
            emit(sfmt("mm_arr_set_s(%s, %s, %s);", f.ptr, f.cnt,
                      val.code));
        } else if (sym->ty == TY_I) {
            emit(sfmt("mm_arr_set_i(%s, %s, %s);", f.ptr, f.cnt,
                      as_int(val)));
        } else {
            emit(sfmt("mm_arr_set_f(%s, %s, %s);", f.ptr, f.cnt,
                      as_flt(val)));
        }
        return;
    }
    if (strcmp(op, "ADD") == 0 || strcmp(op, "SCALE") == 0) {
        struct sym *src, *dst;
        struct val val;
        struct flat sf, df;
        src = arrayref(1);
        expect_op(",");
        val = expr();
        expect_op(",");
        dst = arrayref(1);
        if (src->ty != dst->ty)
            cv_err("%s needs both arrays to be the same type", op);
        sf = array_flat(src);
        df = array_flat(dst);
        if (src->ty == TY_S) {
            if (strcmp(op, "SCALE") == 0)
                cv_err("SCALE does not apply to a string array");
            if (val.ty != TY_S)
                cv_err("a string array needs a string value");
            emit(sfmt("mm_arr_add_s(%s, %s, %s, %s);",
                      sf.ptr, sf.cnt, val.code, df.ptr));
            return;
        }
        {
            const char *fn = sfmt("mm_arr_%s_%s", lower(op),
                                  src->ty == TY_I ? "i" : "f");
            const char *cval = src->ty == TY_I ? as_int(val)
                                               : as_flt(val);
            emit(sfmt("%s(%s, %s, %s, %s);",
                      fn, sf.ptr, sf.cnt, cval, df.ptr));
        }
        return;
    }
    if (strcmp(op, "RANDOMIZE") == 0) {
        if (stmt_end())
            emit("mm_randomize(mm_epoch_now());");
        else
            emit(sfmt("mm_randomize(%s);", as_int(expr())));
        return;
    }
    cv_err("MATH/ARRAY %s is not supported", t->text);
}

/* -- files ----------------------------------------------------------- */

static void do_open(void)
{
    struct val name = expr();
    struct tok *t;
    const char *mode;
    const char *fn;

    if (name.ty != TY_S)
        cv_err("OPEN needs a file name string");
    if (!accept_kw("FOR"))
        cv_err("serial ports (OPEN comspec$ AS #n) are not supported");
    t = nxt();
    if (t->kind != T_ID || (strcmp(t->up, "INPUT") != 0
                            && strcmp(t->up, "OUTPUT") != 0
                            && strcmp(t->up, "APPEND") != 0
                            && strcmp(t->up, "RANDOM") != 0))
        cv_err("OPEN mode must be INPUT, OUTPUT, APPEND or RANDOM");
    mode = sfmt("MM_F_%s", t->up);
    if (!accept_kw("AS"))
        cv_err("OPEN ... FOR ... AS #n");
    fn = channel();
    emit(sfmt("mm_open(%s, %s, %s);", name.code, mode, fn));
}

static void do_close(void)
{
    while (!stmt_end()) {
        const char *fn = channel();
        emit(sfmt("mm_close(%s);", fn));
        if (!accept_op(","))
            break;
    }
}

static void do_fileword(const char *up)
{
    struct val v;

    if (strcmp(up, "FILES") == 0) {
        if (stmt_end()) {
            emit("mm_files(\"\\000\" \"\");");
            return;
        }
        v = expr();
        if (v.ty != TY_S)
            cv_err("FILES needs a file specification string");
        emit(sfmt("mm_files(%s);", v.code));
        skip_statement();        /* an optional sort order */
        return;
    }
    v = expr();
    if (v.ty != TY_S)
        cv_err("%s needs a string", up);
    emit(sfmt("mm_%s(%s);", lower(up), v.code));
    skip_statement();            /* KILL's optional 'all' */
}

/* A variable, possibly an array element, that INPUT can write. */
static struct val input_target(void)
{
    struct tok *t = nxt();
    struct sym *sym;
    struct val r;
    int is_arr;

    if (t->kind != T_ID)
        cv_err("INPUT needs a variable");
    is_arr = is_op("(", 0);
    sym = reference(t->text, 0);
    if (sym->is_const)
        cv_err("'%s' is a CONST", sym->name);
    if (is_arr) {
        if (!sym->is_array)
            cv_err("'%s' is not an array", sym->name);
        r.code = index_of(sym);
        r.ty = sym->ty;
        return r;
    }
    if (sym->is_array)
        cv_err("cannot INPUT into a whole array");
    r.code = sym->acc;
    r.ty = sym->ty;
    return r;
}

static void do_input(void)
{
    const char *chan = "0";

    if (is_op("#", 0)) {
        chan = channel();
        accept_op(",");
    } else {
        struct tok *t = peek(0);
        if (t != NULL && t->kind == T_STR
            && (is_op(";", 1) || is_op(",", 1))) {
            cv.i++;
            emit(sfmt("mm_pr_s(%s);", c_string_literal(t->text)));
            if (is_op(";", 0))
                emit("mm_pr_s(\"\\002\" \"? \");");
            cv.i++;
        } else {
            emit("mm_pr_s(\"\\002\" \"? \");");
        }
    }
    emit(sfmt("mm_input_line(%s);", chan));
    while (!stmt_end()) {
        struct val tgt = input_target();
        if (tgt.ty == TY_S)
            emit(sfmt("mm_sset(%s, mm_input_next());", tgt.code));
        else if (tgt.ty == TY_I)
            emit(sfmt("%s = mm_atoi(mm_input_next());", tgt.code));
        else
            emit(sfmt("%s = mm_atof(mm_input_next());", tgt.code));
        cv.tmp_used = 1;
        if (!accept_op(","))
            break;
    }
}

static void do_line_input(void)
{
    const char *chan = "0";
    struct val tgt;

    if (is_op("#", 0)) {
        chan = channel();
        accept_op(",");
    } else {
        struct tok *t = peek(0);
        if (t != NULL && t->kind == T_STR
            && (is_op(",", 1) || is_op(";", 1))) {
            cv.i++;
            emit(sfmt("mm_pr_s(%s);", c_string_literal(t->text)));
            cv.i++;
        }
    }
    tgt = input_target();
    if (tgt.ty != TY_S)
        cv_err("LINE INPUT needs a string variable");
    emit(sfmt("mm_sset(%s, mm_getline(%s));", tgt.code, chan));
    cv.tmp_used = 1;
}

/* -- assignment / sub call ------------------------------------------- */

/* Scan ahead over an optional bracketed index for a top level '='. */
static int looks_like_assignment(void)
{
    int j = cv.i;

    if (j >= cv.ntoks || cv.toks[j].kind != T_ID)
        return 0;
    j++;
    if (j < cv.ntoks && cv.toks[j].kind == T_OP
        && strcmp(cv.toks[j].text, "(") == 0) {
        int depth = 0;
        while (j < cv.ntoks) {
            struct tok *tk = &cv.toks[j];
            if (tk->kind == T_OP && strcmp(tk->text, "(") == 0) {
                depth++;
            } else if (tk->kind == T_OP && strcmp(tk->text, ")") == 0) {
                depth--;
                if (depth == 0) {
                    j++;
                    break;
                }
            }
            j++;
        }
    }
    return j < cv.ntoks && cv.toks[j].kind == T_OP
        && strcmp(cv.toks[j].text, "=") == 0;
}

static void do_assign_or_call(void)
{
    struct tok *t = peek(0);
    int sfx;
    char *canon = split_suffix(t->text, &sfx);

    if (strcmp(t->up, "MID$") == 0 && !looks_like_assignment()) {
        /* MID$(s$, n, m) = x$   -- looks_like_assignment cannot see
         * it */
    }
    if (routine_get(canon) != NULL && !looks_like_assignment()) {
        struct routine *r;
        struct arglist args;
        struct val v;
        cv.i++;
        r = routine_get(canon);
        call_args(0, &args);
        v = emit_call(r, &args);
        emit(r->is_func ? sfmt("(void)(%s);", v.code)
                        : sfmt("%s;", v.code));
        return;
    }
    if (strcmp(t->up, "MID$") == 0) {
        do_mid_assign();
        return;
    }
    do_assign();
}

static void do_callstmt(void)
{
    struct tok *t = peek(0);
    const char *canon;
    struct routine *r;
    struct arglist args;
    struct val v;
    int sfx;

    if (t != NULL && t->kind == T_STR) {
        cv.i++;
        canon = lower(t->text);
    } else {
        t = nxt();
        canon = split_suffix(t->text, &sfx);
    }
    r = routine_get(canon);
    if (r == NULL)
        cv_err("CALL to unknown subroutine '%s'", canon);
    accept_op(",");
    call_args(0, &args);
    v = emit_call(r, &args);
    emit(sfmt("%s;", v.code));
}

static void do_mid_assign(void)
{
    const char *tgt;
    struct val start, num, v;
    int has_num = 0;

    cv.i++;
    expect_op("(");
    tgt = lvalue_from_here();
    expect_op(",");
    start = expr();
    if (accept_op(",")) {
        num = expr();
        has_num = 1;
    }
    expect_op(")");
    expect_op("=");
    v = expr();
    if (v.ty != TY_S)
        cv_err("MID$() assignment needs a string");
    emit(sfmt("mm_mid_assign(%s, %s, %s, %s);",
              tgt, as_int(start), has_num ? as_int(num) : "-1",
              v.code));
}

static const char *lvalue_from_here(void)
{
    struct tok *t = nxt();
    struct sym *s;

    if (t->kind != T_ID)
        cv_err("variable expected");
    s = reference(t->text, is_op("(", 0));
    if (s->is_array)
        return index_of(s);
    return s->acc;
}

static void do_assign(void)
{
    struct tok *t = nxt();
    int sfx;
    char *canon;
    struct sym *s;
    struct val v;
    const char *target;
    int is_arr;

    if (t->kind != T_ID)
        cv_err("assignment target expected");
    canon = split_suffix(t->text, &sfx);

    /* assignment to the enclosing function's name = set return value */
    if (cv.cur != NULL && cv.cur->is_func
        && strcmp(canon, cv.cur->name) == 0) {
        int ty;
        expect_op("=");
        v = expr();
        ty = cv.cur->ty;
        if (ty == TY_S) {
            if (v.ty != TY_S)
                cv_err("function '%s' returns a string", canon);
            emit(sfmt("mm_sset(__ret, %s);", v.code));
        } else if (ty == TY_I) {
            emit(sfmt("__ret = %s;", as_int(v)));
        } else {
            emit(sfmt("__ret = %s;", as_flt(v)));
        }
        return;
    }

    is_arr = is_op("(", 0);
    s = reference(t->text, 0);
    if (is_arr) {
        if (!s->is_array)
            cv_err("'%s' is not an array", canon);
        target = index_of(s);
    } else {
        if (s->is_array)
            cv_err("cannot assign to whole array '%s'", canon);
        target = s->acc;
    }
    if (s->is_const)
        cv_err("'%s' is a CONST and cannot be assigned to", canon);
    expect_op("=");
    v = expr();
    if (s->ty == TY_S) {
        if (v.ty != TY_S)
            cv_err("cannot assign a number to string '%s'", canon);
        emit(sfmt("mm_sset(%s, %s);", target, v.code));
    } else if (s->ty == TY_I) {
        emit(sfmt("%s = %s;", target, as_int(v)));
    } else {
        emit(sfmt("%s = %s;", target, as_flt(v)));
    }
}

/* -- IF -------------------------------------------------------------- */

static char *cond(void)
{
    struct val v = expr();

    if (v.ty == TY_S)
        cv_err("a string cannot be used as a condition");
    return sfmt("(%s) != 0", v.code);
}

static void do_if(void)
{
    char *c = cond();

    if (!accept_kw("THEN")) {
        if (is_kw("GOTO", 0)) {
            /* pass */
        } else {
            cv_err("IF without THEN");
        }
    }
    if (stmt_end()) {
        /* block IF */
        emit(sfmt("if (%s) {", c));
        cv.indent++;
        push_block("if", NULL, 0);
        return;
    }
    /* single line IF */
    emit(sfmt("if (%s) {", c));
    cv.indent++;
    if (is_kw("GOTO", 0)) {
        cv.i++;
        do_goto();
    } else if (peek(0) != NULL && peek(0)->kind == T_NUM) {
        do_goto();              /* IF expr THEN <line number> */
    } else {
        inline_statements();
    }
    cv.indent--;
    if (accept_kw("ELSE")) {
        emit("} else {");
        cv.indent++;
        inline_statements();
        cv.indent--;
    }
    emit("}");
}

static void inline_statements(void)
{
    int depth = cv.nblocks;

    cv.inline_depth++;
    while (!at_end() && !is_kw("ELSE", 0)) {
        if (accept_op(":"))
            continue;
        statement();
        if (cv.nblocks != depth)
            cv_err("a single line IF cannot open a multi-line block");
    }
    cv.inline_depth--;
}

static void do_elseif(void)
{
    char *c;

    if (cv.nblocks == 0
        || strcmp(cv.blocks[cv.nblocks - 1].kind, "if") != 0)
        cv_err("ELSEIF without IF");
    c = cond();
    accept_kw("THEN");
    cv.indent--;
    emit(sfmt("} else if (%s) {", c));
    cv.indent++;
}

static void do_else(void)
{
    if (cv.nblocks == 0
        || strcmp(cv.blocks[cv.nblocks - 1].kind, "if") != 0)
        cv_err("ELSE without IF");
    cv.indent--;
    emit("} else {");
    cv.indent++;
}

static void close_block(const char *kind)
{
    if (cv.nblocks == 0
        || strcmp(cv.blocks[cv.nblocks - 1].kind, kind) != 0)
        cv_err("mismatched end of %s block", kind);
    cv.nblocks--;
    cv.indent--;
    emit("}");
}

/* -- FOR ------------------------------------------------------------- */

static void do_for(void)
{
    struct tok *t = nxt();
    int sfx;
    char *canon;
    struct sym *s;
    struct val start, limit;
    struct val step = { NULL, TY_NONE };
    int has_step = 0;
    const char *ct;
    const char *(*conv)(struct val);
    char *lim;
    const char *cmp_txt, *inc;

    if (t->kind != T_ID)
        cv_err("FOR needs a counter variable");
    canon = split_suffix(t->text, &sfx);
    s = reference(t->text, 0);
    if (s->ty == TY_S)
        cv_err("FOR counter cannot be a string");
    if (s->is_array)
        cv_err("FOR counter cannot be an array");
    expect_op("=");
    start = expr();
    if (!accept_kw("TO"))
        cv_err("FOR without TO");
    limit = expr();
    if (accept_kw("STEP")) {
        step = expr();
        has_step = 1;
    }

    ct = ctype_of(s->ty);
    conv = (s->ty == TY_I) ? as_int : as_flt;
    lim = newtmp("lim");
    emit("{");
    cv.indent++;
    emit(sfmt("%s %s = %s;", ct, lim, conv(limit)));
    if (!has_step) {
        cmp_txt = sfmt("%s <= %s", s->acc, lim);
        inc = sfmt("%s += 1", s->acc);
    } else if (is_literal_number(step)) {
        const char *p = step.code;
        int neg;
        while (*p == '(')
            p++;
        neg = (*p == '-');
        cmp_txt = sfmt("%s %s %s", s->acc, neg ? ">=" : "<=", lim);
        inc = sfmt("%s += %s", s->acc, conv(step));
    } else {
        char *stp = newtmp("stp");
        emit(sfmt("%s %s = %s;", ct, stp, conv(step)));
        cmp_txt = sfmt("(%s >= 0 ? %s <= %s : %s >= %s)",
                       stp, s->acc, lim, s->acc, lim);
        inc = sfmt("%s += %s", s->acc, stp);
    }
    /* the comparison is against plain variables: no temps, and so no
     * per-iteration release point */
    emit(sfmt("for (%s = %s; %s; %s) {",
              s->acc, conv(start), cmp_txt, inc));
    cv.indent++;
    push_block("for", pstr(canon), 0);
}

/* Build a loop condition and say whether evaluating it consumes
 * string temporaries.  Only then is the per-iteration release point
 * needed: an unconditional one is a library call per trip that costs
 * more than the body of a tight FOR loop. */
static char *cond_release(int *used)
{
    int outer = cv.tmp_used;
    char *c;

    cv.tmp_used = 0;
    c = cond();
    *used = cv.tmp_used;
    cv.tmp_used = outer || cv.tmp_used;
    return c;
}

static void do_next(void)
{
    const char *names[MAXTOKS];
    int nnames = 0;
    int k;

    while (!stmt_end()) {
        struct tok *t = nxt();
        int sfx;
        if (t->kind != T_ID)
            cv_err("bad NEXT");
        if (nnames >= MAXTOKS)
            mm_error("line %d: too many names in NEXT", cv.lineno);
        names[nnames++] = split_suffix(t->text, &sfx);
        if (!accept_op(","))
            break;
    }
    if (nnames == 0)
        names[nnames++] = NULL;
    for (k = 0; k < nnames; k++) {
        const char *nm = names[k];
        struct block *blk;
        if (cv.nblocks == 0
            || strcmp(cv.blocks[cv.nblocks - 1].kind, "for") != 0)
            cv_err("NEXT without FOR");
        blk = &cv.blocks[cv.nblocks - 1];
        if (nm != NULL && strcmp(blk->a, nm) != 0)
            cv_err("NEXT %s does not match FOR %s", nm, blk->a);
        cv.nblocks--;
        cv.indent--;
        emit("}");
        cv.indent--;
        emit("}");
    }
}

/* -- DO / LOOP / WHILE ----------------------------------------------- */

static void do_do(void)
{
    if (accept_kw("WHILE")) {
        int used;
        char *c = cond_release(&used);
        emit(sfmt("while (%s) {", used ? loop_cond(c) : c));
        cv.indent++;
        push_block("do", "head", 0);
        return;
    }
    if (accept_kw("UNTIL")) {
        int used;
        char *c = cond_release(&used);
        c = sfmt("!(%s)", c);
        emit(sfmt("while (%s) {", used ? loop_cond(c) : c));
        cv.indent++;
        push_block("do", "head", 0);
        return;
    }
    emit("do {");
    cv.indent++;
    push_block("do", "tail", 0);
}

static void do_loop(void)
{
    struct block blk;

    if (cv.nblocks == 0
        || strcmp(cv.blocks[cv.nblocks - 1].kind, "do") != 0)
        cv_err("LOOP without DO");
    blk = cv.blocks[--cv.nblocks];
    cv.indent--;
    if (strcmp(blk.a, "head") == 0) {
        if (!stmt_end())
            cv_err("this DO already has its test at the top");
        emit("}");
        return;
    }
    if (accept_kw("UNTIL")) {
        int used;
        char *c = cond_release(&used);
        c = sfmt("!(%s)", c);
        emit(sfmt("} while (%s);", used ? loop_cond(c) : c));
    } else if (accept_kw("WHILE")) {
        int used;
        char *c = cond_release(&used);
        emit(sfmt("} while (%s);", used ? loop_cond(c) : c));
    } else {
        emit("} while (1);");
    }
}

static void do_while(void)
{
    int used;
    char *c = cond_release(&used);

    emit(sfmt("while (%s) {", used ? loop_cond(c) : c));
    cv.indent++;
    push_block("while", NULL, 0);
}

/* -- SELECT CASE ----------------------------------------------------- */

static void do_select(void)
{
    struct val v;
    char *name;

    if (!accept_kw("CASE"))
        cv_err("SELECT without CASE");
    v = expr();
    name = newtmp("sel");
    emit("{");
    cv.indent++;
    if (v.ty == TY_S) {
        emit(sfmt("char %s[MM_STRSZ]; mm_sset(%s, %s);",
                  name, name, v.code));
        cv.tmp_used = 1;
    } else {
        emit(sfmt("%s %s = %s;", ctype_of(v.ty), name, v.code));
    }
    emit("if (0) {");
    cv.indent++;
    push_block("select", pstr(name), v.ty);
}

static void do_case(void)
{
    struct block *blk;
    const char *name;
    const char *joined = NULL;
    int ty;

    if (cv.nblocks == 0
        || strcmp(cv.blocks[cv.nblocks - 1].kind, "select") != 0)
        cv_err("CASE outside SELECT CASE");
    blk = &cv.blocks[cv.nblocks - 1];
    name = blk->a;
    ty = blk->ty;
    cv.indent--;
    if (accept_kw("ELSE")) {
        emit("} else {");
        cv.indent++;
        return;
    }
    for (;;) {
        const char *tst = case_test(name, ty);
        joined = joined ? sfmt("%s || %s", joined, tst) : tst;
        if (!accept_op(","))
            break;
    }
    emit(sfmt("} else if (%s) {", joined));
    cv.indent++;
}

/* the Python's nested cmpv closure, with name/ty made explicit */
static const char *cmpv(const char *name, int ty, const char *op,
                        const char *code, int cty)
{
    if (ty == TY_S) {
        if (cty != TY_S)
            cv_err("CASE type mismatch");
        return sfmt("(mm_scmp(%s, %s) %s 0)", name, code, op);
    }
    return sfmt("((%s) %s (%s))", name, op, code);
}

static int is_cmp_op(const char *s)
{
    return strcmp(s, "=") == 0 || strcmp(s, "<>") == 0
        || strcmp(s, "<") == 0 || strcmp(s, ">") == 0
        || strcmp(s, "<=") == 0 || strcmp(s, ">=") == 0;
}

static const char *cop(const char *s)
{
    if (strcmp(s, "=") == 0)
        return "==";
    if (strcmp(s, "<>") == 0)
        return "!=";
    return s;
}

static const char *case_test(const char *name, int ty)
{
    struct tok *t;
    struct val lo, hi;

    if (accept_kw("IS")) {
        const char *op;
        struct val v;
        t = nxt();
        if (t->kind != T_OP || !is_cmp_op(t->text))
            cv_err("CASE IS needs a comparison operator");
        op = cop(t->text);
        v = expr();
        return cmpv(name, ty, op, v.code, v.ty);
    }
    t = peek(0);
    if (t != NULL && t->kind == T_OP && is_cmp_op(t->text)) {
        const char *op;
        struct val v;
        cv.i++;
        op = cop(t->text);
        v = expr();
        return cmpv(name, ty, op, v.code, v.ty);
    }
    lo = expr();
    if (accept_kw("TO")) {
        const char *c1, *c2;
        hi = expr();
        c1 = cmpv(name, ty, ">=", lo.code, lo.ty);
        c2 = cmpv(name, ty, "<=", hi.code, hi.ty);
        return sfmt("(%s && %s)", c1, c2);
    }
    return cmpv(name, ty, "==", lo.code, lo.ty);
}

/* -- EXIT / GOTO / END ----------------------------------------------- */

static void do_exit(void)
{
    int k;

    if (accept_kw("SUB")) {
        emit(sfmt("%s return;", routine_exit()));
        return;
    }
    if (accept_kw("FUNCTION")) {
        emit(sfmt("%s return __ret;", routine_exit()));
        return;
    }
    if (accept_kw("FOR") || accept_kw("DO")) {
        emit("break;");
        return;
    }
    if (stmt_end()) {
        /* bare EXIT: the manual documents it as "exit a DO loop", but
         * real programs also use it inside a SUB to mean EXIT SUB, so
         * take whichever the enclosing block actually is */
        for (k = cv.nblocks - 1; k >= 0; k--) {
            const char *kind = cv.blocks[k].kind;
            if (strcmp(kind, "for") == 0 || strcmp(kind, "do") == 0
                || strcmp(kind, "while") == 0) {
                emit("break;");
                return;
            }
            if (strcmp(kind, "routine") == 0)
                break;
        }
        if (cv.cur != NULL) {
            cv_warn("bare EXIT inside %s with no enclosing loop; "
                    "treated as EXIT %s", cv.cur->name,
                    cv.cur->is_func ? "FUNCTION" : "SUB");
            if (cv.cur->is_func)
                emit(sfmt("%s return __ret;", routine_exit()));
            else
                emit(sfmt("%s return;", routine_exit()));
            return;
        }
        cv_err("bare EXIT is outside any loop, SUB or FUNCTION");
    }
    cv_err("unknown EXIT variant");
}

static void note_goto(const char *canon)
{
    struct label *l;
    int depth = 0;
    int k;

    for (k = 0; k < cv.nblocks; k++)
        if (strcmp(cv.blocks[k].kind, "routine") != 0)
            depth++;
    l = label_rec(canon);
    if (!l->has_goto || depth < l->goto_depth) {
        l->has_goto = 1;
        l->goto_depth = depth;
    }
}

static void do_goto(void)
{
    struct tok *t = nxt();
    const char *canon = NULL;
    struct label *l;
    int sfx;

    if (t->kind == T_NUM && strcmp(t->up, "I") == 0)
        canon = t->text;
    else if (t->kind != T_ID)
        cv_err("GOTO needs a label or line number");
    else
        canon = split_suffix(t->text, &sfx);
    l = label_rec(canon);
    if (!l->placed)
        cv_err("unknown label '%s'", t->text);
    l->used = 1;
    note_goto(canon);
    emit(sfmt("goto %s;", clabel(canon)));
}

static void do_end(void)
{
    /* Inside a single-line IF, END SUB means RETURN NOW, not "the
     * routine's text stops here" -  is the
     * ordinary MMBasic way to leave a SUB early, and closing the block
     * there would end the routine in the middle of itself. */
    if (cv.inline_depth) {
        if (accept_kw("SUB")) {
            emit(sfmt("%s return;", routine_exit()));
            return;
        }
        if (accept_kw("FUNCTION")) {
            emit(sfmt("%s return __ret;", routine_exit()));
            return;
        }
    }
    if (accept_kw("SUB")) {
        close_routine(0);
        return;
    }
    if (accept_kw("FUNCTION")) {
        close_routine(1);
        return;
    }
    if (accept_kw("IF")) {
        close_block("if");
        return;
    }
    if (accept_kw("SELECT")) {
        close_block("select");
        cv.indent--;
        emit("}");
        return;
    }
    skip_statement();
    emit("mm_end();");
}

/* -- SUB / FUNCTION bodies ------------------------------------------- */

static void open_routine(int is_func)
{
    struct tok *t = nxt();
    int sfx, k;
    char *canon = split_suffix(t->text, &sfx);
    struct routine *r = routine_get(canon);

    (void)is_func;              /* the Python ignores it too */
    if (r == NULL)
        cv_err("internal: routine '%s' not found", canon);
    skip_statement();
    cv.cur = r;
    cv.out = &cv.out_body;
    cv.indent = 0;
    raw("");
    raw(sfmt("%s {", signature(r)));
    cv.indent = 1;
    emit("unsigned __mark = mm_mark(); (void)__mark;");
    if (r->heap_locals)
        emit(sfmt("struct mm_l_%s *__L = mm_lheap(sizeof *__L);",
                  r->cname));
    /* hoist every local declaration to the top of the C function, in
     * declaration order so the output does not depend on the host
     * Python's dictionary ordering */
    for (k = 0; k < r->nlocal_order; k++) {
        struct sym *s = locals_get(r, r->local_order[k]);
        if (s->is_param)
            continue;
        emit_local_decl(s);
    }
    if (r->is_func) {
        if (r->ty == TY_S)
            emit("__ret[0] = 0; __ret[1] = 0;");
        else
            emit(sfmt("%s __ret = 0;", ctype_of(r->ty)));
    }
    push_block("routine", NULL, 0);
}

static void emit_local_decl(struct sym *s)
{
    const char *pfx = s->is_static ? "static " : "";

    /* An array or a string that is not STATIC lives in the invocation's
     * heap block, declared once in its struct and zeroed by mm_lheap;
     * there is nothing to declare here. */
    if (!s->is_static && (s->is_array || s->ty == TY_S))
        return;

    if (s->is_static && s->has_init)
        emit(sfmt("static int __once_%s = 0;", dunder("", s->name)));
    if (s->is_array) {
        const char *dims = "";
        int k;
        for (k = 0; k < s->ndims; k++)
            dims = sfmt("%s[%s]", dims, s->dims[k]);
        if (s->ty == TY_S)
            emit(sfmt("%schar %s%s[MM_STRSZ];", pfx, s->acc, dims));
        else
            emit(sfmt("%s%s %s%s;", pfx, ctype_of(s->ty), s->acc,
                      dims));
        if (!s->is_static)
            emit(sfmt("memset(%s, 0, sizeof %s);", s->acc, s->acc));
    } else if (s->ty == TY_S) {
        emit(sfmt("%schar %s[MM_STRSZ];", pfx, s->acc));
        if (!s->is_static)
            emit(sfmt("%s[0] = 0; %s[1] = 0;", s->acc, s->acc));
    } else {
        if (s->is_static)
            emit(sfmt("static %s %s;", ctype_of(s->ty), s->acc));
        else
            emit(sfmt("%s %s = 0;", ctype_of(s->ty), s->acc));
    }
}

char *signature(struct routine *r)
{
    const char *joined = NULL;
    const char *ret;
    int k;

    if (r->is_func && r->ty == TY_S)
        joined = "char *__ret";
    for (k = 0; k < r->nparams; k++) {
        struct sym *p = r->params[k];
        char *nm = dunder("p_", p->name);
        const char *part;
        if (p->is_array) {
            if (p->ty == TY_S)
                part = sfmt("char (*%s)[MM_STRSZ]", nm);
            else
                part = sfmt("%s *%s", ctype_of(p->ty), nm);
            joined = joined ? sfmt("%s, %s", joined, part) : part;
            /* BOUND() inside the routine reads the caller's bounds */
            part = sfmt("const MMINTEGER *__b_%s",
                        dunder("", p->name));
        } else if (p->ty == TY_S) {
            part = sfmt("char *%s", nm);
        } else if (p->byref) {
            part = sfmt("%s *%s", ctype_of(p->ty), nm);
        } else {
            part = sfmt("%s %s", ctype_of(p->ty), nm);
        }
        joined = joined ? sfmt("%s, %s", joined, part) : part;
    }
    if (joined == NULL)
        joined = "void";
    if (!r->is_func)
        ret = "void ";
    else if (r->ty == TY_S)
        ret = "char *";
    else
        ret = sfmt("%s ", ctype_of(r->ty));
    return sfmt("%s%s(%s)", ret, r->cname, joined);
}

static void close_routine(int is_func)
{
    (void)is_func;              /* the Python ignores it too */
    if (cv.nblocks == 0
        || strcmp(cv.blocks[cv.nblocks - 1].kind, "routine") != 0)
        cv_err("END SUB/FUNCTION without SUB/FUNCTION");
    cv.nblocks--;
    if (cv.cur != NULL && cv.cur->is_func)
        emit(sfmt("%s return __ret;", routine_exit()));
    else
        emit(routine_exit());
    cv.indent = 0;
    raw("}");
    cv.cur = NULL;
    cv.out = &cv.out_main;
    cv.indent = 1;
}

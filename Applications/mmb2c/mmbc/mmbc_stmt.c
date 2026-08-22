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

/* Which program serves this LOAD, or NULL if it is not one of them.
   A function rather than a local because cc1 compiles this file on the
   board and C89 has no mid-block declarations - and is_kw() is pure
   lookahead, so asking twice costs nothing and consumes nothing. */
static const char *load_prog(const char *up);

static void do_print(void);
static char *prcall(const char *chan, const char *what, const char *arg);
static const char *int_handler(void);
static const char *setpin_pull(void);
static void do_settick(void);
static void do_i2c2(void);
static void do_i2c0(void);
static void do_onewire(void);
static void do_web(void);
static void do_web_page(void);
static void do_spi(void);
static void do_on_key(void);
static const char *int_target(void);
static void do_longstring(void);
static const char *gosub_key(void);
static void do_gosub(void);
static void emit_gosub(const char *canon, const char *disp);
static void do_return(void);
static void do_read(void);
static void do_restore(void);
static void do_sort(void);
static void do_pixels(void);
static const char *shortest(const char **counts, int n);
static void do_inc(void);
static void do_cat(void);
static void do_erase(void);
static void do_on_goto(void);
static void do_on_error(void);
static void do_array_cmd(int is_math);
static void do_open(void);
static void do_close(void);
static void do_fileword(const char *up);
struct val input_target(int *cap);   /* shared: MATH(BASE64) uses it */
static void do_input(void);
static void do_line_input(void);
static int looks_like_assignment(void);
static void do_assign_or_call(void);
static void do_callstmt(void);
static void do_mid_assign(void);
static const char *lvalue_from_here(void);
static void comms_tx(const char *what, const char *n,
                     const char *callfmt, const char *bytesfmt);
static void comms_rx(const char *what, const char *n,
                     const char *callfmt, const char *bytesfmt);
static void do_assign(void);
static void store(const char *target, const char *val, int ty);
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

/* MEMORY|COMPRESSED addr, x, y [, t] - shared by BLIT and SPRITE: on
 * an LCD PicoMite the two are one command. */
static void do_blit_memform(void)
{
    const char *a, *x, *y;
    const char *blank = "-1LL";
    int is_mem = is_kw("MEMORY", 1);

    cv.i += 2;
    a = as_int(expr());
    expect_op(",");
    x = as_int(expr());
    expect_op(",");
    y = as_int(expr());
    if (accept_op(","))
        blank = as_int(expr());
    emit(sfmt("mmb_blit_%s(%s, %s, %s, %s);",
              is_mem ? "mem" : "comp", a, x, y, blank));
}

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

    if (skip_type_block(up))
        return;
    /* A SUB the program defines WINS over a statement of the same name -
       the rule the expression parser already applies to functions, and
       for the same reason: a program written before a command existed
       has to keep working.  tests/t2.bas has a SUB Fill, which was a
       plain sub call until FILL became a drawing command, and this is
       what keeps it one.

       Structural words are excluded: END, PRINT, FOR and the rest are
       syntax, not commands, and a SUB called END could not be called
       anyway.  Everything else is fair game. */
    if (t->kind == T_ID && !kw_in(up)) {
        int sfx;

        if (routine_name_known(split_suffix(t->text, &sfx))) {
            do_assign_or_call();
            return;
        }
    }
    if (strcmp(up, "STRUCT") == 0) {
        cv.i++;
        do_struct();
        return;
    }
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
    /* MMBasic spells it two ways and AllCommands.h binds BOTH to
     * cmd_else: "Else If" is one command name there, not an ELSE with
     * an IF after it.  Taken as two words this opened a nested block
     * that wanted its own ENDIF, so a program written the spelling the
     * manual uses died with "unterminated if block". */
    if (strcmp(up, "ELSE") == 0 && is_kw("IF", 1)) {
        cv.i += 2;
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
    if (strcmp(up, "FLUSH") == 0) {
        /* FLUSH #n - get what has been written onto the card.  One
           channel, as MMBasic takes one; CLOSE above accepts a list and
           this deliberately does not, because cmd_flush does not. */
        cv.i++;
        emit(sfmt("mm_flush(%s);", channel()));
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
    if (strcmp(up, "REDIM") == 0) {
        cv.i++;
        do_redim();
        return;
    }
    if (strcmp(up, "POKE") == 0) {
        /* POKE BYTE addr, value   and its wider relatives.

           The width is a bare keyword, exactly as PEEK's is, so it is
           read here rather than as an argument.  MMBasic's other POKE
           forms - VAR, DISPLAY, PROGMEM - address the interpreter's own
           structures and have no equivalent. */
        struct tok *t;
        const char *fn = NULL, *addr;
        struct val v;
        int isfloat = 0;

        cv.i++;
        t = nxt();
        if (t->kind == T_ID) {
            if (strcmp(t->up, "BYTE") == 0)         fn = "mmpk_poke_byte";
            else if (strcmp(t->up, "SHORT") == 0)   fn = "mmpk_poke_short";
            else if (strcmp(t->up, "WORD") == 0)    fn = "mmpk_poke_word";
            else if (strcmp(t->up, "INTEGER") == 0) fn = "mmpk_poke_integer";
            else if (strcmp(t->up, "FLOAT") == 0) {
                fn = "mmpk_poke_float";
                isfloat = 1;
            }
        }
        if (fn == NULL)
            cv_err("POKE %s is not supported; translated are BYTE, "
                   "SHORT, WORD, INTEGER and FLOAT", t->text);
        addr = as_int(expr());
        expect_op(",");
        v = expr();
        cv.uses_peek = 1;
        emit(sfmt("%s(%s, %s);", fn, addr,
                  isfloat ? as_flt(v) : as_int(v)));
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
        /* CLS [colour] - MMBasic floods the write buffer with it, so
           this clears the off-screen framebuffer when one is selected,
           not the screen.  No colour means the background COLOUR set,
           which MM_CUR asks for. */
        const char *col = "MM_CUR";

        cv.i++;
        if (!stmt_end())
            col = as_int(expr());
        emit(sfmt("mm_cls(%s);", col));
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
        /* FRAMEBUFFER CREATE | LAYER | CLOSE [F|L] | WRITE N|F|L |
                       COPY s, d [, B] | MERGE [c] | WAIT

           MMBasic's Draw.c cmd_framebuffer.  Two off-screen buffers: F,
           and the LAYER, which is another framebuffer in every respect
           except that MERGE puts it OVER F on the way to the screen,
           skipping a transparent colour.

           That is MMBasic's TFT model rather than its VGA/HDMI one,
           where the layer is composited at scanout instead.  The choice
           is argued in PC3-LAYER-MERGE.md and comes down to SRAM: a
           scanout-time layer must live where core1 can DMA it, which is
           40K off every process forever.  A program written for a
           PicoMite driving an ILI9341 runs unchanged.

           A mode change discards the buffers, both here and in the
           kernel, so CREATE belongs after MODE - which is also where
           MMBasic wants it, setmode() closing every buffer. */
        cv.i++;
        if (accept_kw("CREATE")) {
            emit("mm_fb_create(1);");
            return;
        }
        if (accept_kw("LAYER")) {
            /* FRAMEBUFFER LAYER [transparent] - the optional colour
               (0-15, default 0) is the transparent index a MERGE uses
               when it names none.  The firmware keeps it in
               transparentlow/high (V5.08.00 Draw.c:7375-7381), so it
               is run-time state and lives in an emitted global, not
               in the translator. */
            if (!stmt_end()) {
                cv.uses_fbt = 1;
                emit(sfmt("__mm_fbt = (int)(%s);", as_int(expr())));
            }
            emit("mm_fb_create(2);");
            return;
        }
        if (accept_kw("CLOSE")) {
            /* CLOSE L closes the layer, CLOSE or CLOSE F the other */
            int which = accept_kw("L") ? 2 : 1;

            if (which == 1)
                accept_kw("F");
            emit(sfmt("mm_fb_close(%d);", which));
            return;
        }
        if (accept_kw("MERGE")) {
            /* FRAMEBUFFER MERGE [colour] - the transparent index, 0 to
               15, defaulting to 0 as MMBasic's does. */
            /* FRAMEBUFFER MERGE [colour] [, B] - the transparent
               index, 0 to 15, defaulting to 0 as MMBasic's does.

               MMBasic's second argument asks for the merge to run on
               the OTHER CORE so BASIC carries on (FrameBuffer.c:1071
               pushes it down the multicore FIFO).  Accepted and not
               acted on, and that is not a divergence: on a VGA display
               the reference ignores it too - FrameBuffer.c:1084 sets
               background = 0 for every DISPLAY_TYPE from VGA222 up,
               which is this machine's class.  So the merge happens in
               the syscall, exactly as it does there.  R and A name
               modes this display has no equivalent for and are refused
               rather than quietly taken as B. */
            const char *c = cv.uses_fbt ? "__mm_fbt" : "0";

            if (!stmt_end()) {
                if (!is_op(",", 0))
                    c = as_int(expr());
                if (accept_op(",")) {
                    if (!accept_kw("B"))
                        cv_err("FRAMEBUFFER MERGE takes only B here");
                }
            }
            emit(sfmt("mm_fb_merge(%s);", c));
            return;
        }
        if (accept_kw("WRITE")) {
            emit(sfmt("mm_fb_write(%s);", fb_buf()));
            return;
        }
        if (accept_kw("COPY")) {
            const char *s, *d;
            int b = 0;
            s = fb_buf();
            expect_op(",");
            d = fb_buf();
            if (accept_op(",")) {
                if (!accept_kw("B"))
                    cv_err("FRAMEBUFFER COPY takes only B here");
                b = 1;
            }
            emit(sfmt("mm_fb_copy(%s, %s, %d);", s, d, b));
            return;
        }
        if (accept_kw("WAIT")) {
            emit("mm_fb_wait();");
            return;
        }
        cv_err("only FRAMEBUFFER CREATE, LAYER, CLOSE, WRITE, COPY, "
               "MERGE and WAIT are translated");
    }
    if (strcmp(up, "SYSTEM") == 0 || load_prog(up) != NULL
        || (strcmp(up, "SAVE") == 0 && is_kw("IMAGE", 1))) {
        /* SYSTEM prog$ [, arg ...]        run a program and wait
           SAVE IMAGE f$ [, x, y, w, h]    both are programs
           LOAD IMAGE f$ [, x, y]
           LOAD BMP   f$ [, x, y]          the reference's own synonym
           LOAD JPG   f$ [, x, y [, mode [, xi, yi [, scale]]]]
           LOAD PNG   f$ [, x, y [, transparent [, cutoff]]]

           An argv, not a command line: nothing to quote and no shell
           in the middle.  MMBasic has no SYSTEM - it is firmware with
           nothing to run - so that spelling is ours, but SAVE IMAGE
           and the LOAD family are the interpreter's own and are simply
           handed to /usr/bin/saveimage, /usr/bin/loadimage and
           /usr/bin/loadjpg and /usr/bin/loadpng.

           LOAD JPG's arguments are passed straight through in the
           reference's order - x, y, dither mode, image offsets, scale -
           so a program written for a PicoMite needs no edit.  The mode
           is parsed and ignored there, as it is in loadimage: see the
           note in loadjpg.c about dithering. */
        const char *prog = NULL;
        int first = 1;

        if (strcmp(up, "SYSTEM") == 0) {
            cv.i++;
        } else {
            /* BEFORE the tokens are consumed: load_prog looks ahead
               one token, and cv.i += 2 eats the very keyword it reads. */
            prog = (strcmp(up, "SAVE") == 0) ? "saveimage" : load_prog(up);
            cv.i += 2;
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
    if (strcmp(up, "SPRITE") == 0) {
        /* The SPRITE family (graphics/Sprite.c), engine in
           mmb_sprite.h on the BLIT row workhorses.  Deferred there
           and refused here by name: SCROLL (Phase 4 of
           PLAN-games.md - it wants the kernel's SCROLL2) and
           LOADBMP (wants the BMP decoder).  LOADPNG translates. */
        static const char *const nospr[] = {
            "LOADBMP", NULL
        };
        int si;

        cv.uses_sprite = 1;
        cv.uses_blit = 1;
        if (is_kw("MEMORY", 1) || is_kw("COMPRESSED", 1)) {
            /* On an LCD PicoMite SPRITE and BLIT are one command
               (V5.08.00's blitother serves both spellings), so the
               memory forms are BLIT MEMORY under another name. */
            do_blit_memform();
            return;
        }
        if (is_kw("LOADPNG", 1)) {
            /* SPRITE LOADPNG [#]n, f$ [, transparent [, cutoff]]

               The decoding is /usr/bin/loadpng's, in another process,
               and the sprite comes back down a pipe - see mms_loadpng.
               transparent carries MMBasic's sign trick (-n = substitute
               n for opaque black) and is passed through untouched;
               cutoff defaults to 30 here, not LOAD PNG's 20, as in the
               reference. */
            const char *n, *f, *t = "0LL", *c = "30LL";

            cv.i += 2;
            accept_op("#");
            n = as_int(expr());
            expect_op(",");
            f = as_str(expr());
            if (accept_op(",")) {
                t = as_int(expr());
                if (accept_op(","))
                    c = as_int(expr());
            }
            emit(sfmt("mms_loadpng(%s, %s, %s, %s);", n, f, t, c));
            return;
        }
        if (is_kw("SHOW", 1)) {
            const char *n, *x, *y, *layer;
            const char *flags = "0LL", *ontop = "0LL";
            const char *safe = is_kw("SAFE", 2) ? "1LL" : "0LL";

            cv.i += (safe[0] == '1') ? 3 : 2;
            accept_op("#");
            n = as_int(expr());
            expect_op(",");
            x = as_int(expr());
            expect_op(",");
            y = as_int(expr());
            expect_op(",");
            layer = as_int(expr());
            if (accept_op(",")) {
                if (!is_op(",", 0))
                    flags = as_int(expr());
                if (safe[0] == '1' && accept_op(","))
                    ontop = as_int(expr());
            }
            emit(sfmt("mms_show(%s, %s, %s, %s, %s, %s, %s);",
                      n, x, y, layer, flags, safe, ontop));
            return;
        }
        if (is_kw("HIDE", 1)) {
            const char *n, *safe;

            if (is_kw("ALL", 2)) {
                cv.i += 3;
                emit("mms_hide_all();");
                return;
            }
            safe = is_kw("SAFE", 2) ? "1LL" : "0LL";
            cv.i += (safe[0] == '1') ? 3 : 2;
            accept_op("#");
            n = as_int(expr());
            emit(sfmt("mms_hide(%s, %s);", n, safe));
            return;
        }
        if (is_kw("RESTORE", 1)) {
            cv.i += 2;
            emit("mms_restore();");
            return;
        }
        if (is_kw("MOVE", 1)) {
            cv.i += 2;
            emit("mms_move();");
            return;
        }
        if (is_kw("WRITE", 1)) {
            const char *n, *x, *y;
            const char *flags = "4LL";

            cv.i += 2;
            accept_op("#");
            n = as_int(expr());
            expect_op(",");
            x = as_int(expr());
            expect_op(",");
            y = as_int(expr());
            if (accept_op(","))
                flags = as_int(expr());
            emit(sfmt("mms_write(%s, %s, %s, %s);", n, x, y, flags));
            return;
        }
        if (is_kw("READ", 1)) {
            const char *n, *x, *y, *w, *h;

            cv.i += 2;
            accept_op("#");
            n = as_int(expr());
            expect_op(",");
            x = as_int(expr());
            expect_op(",");
            y = as_int(expr());
            expect_op(",");
            w = as_int(expr());
            expect_op(",");
            h = as_int(expr());
            emit(sfmt("mms_read(%s, %s, %s, %s, %s);", n, x, y, w, h));
            return;
        }
        if (is_kw("NEXT", 1)) {
            const char *n, *x, *y;

            cv.i += 2;
            accept_op("#");
            n = as_int(expr());
            expect_op(",");
            x = as_int(expr());
            expect_op(",");
            y = as_int(expr());
            emit(sfmt("mms_next(%s, %s, %s);", n, x, y));
            return;
        }
        if (is_kw("COPY", 1)) {
            const char *n, *first, *cnt;

            cv.i += 2;
            accept_op("#");
            n = as_int(expr());
            expect_op(",");
            accept_op("#");
            first = as_int(expr());
            expect_op(",");
            cnt = as_int(expr());
            emit(sfmt("mms_copy(%s, %s, %s);", n, first, cnt));
            return;
        }
        if (is_kw("SWAP", 1)) {
            const char *n, *rn;
            const char *flags = "0LL";

            cv.i += 2;
            accept_op("#");
            n = as_int(expr());
            expect_op(",");
            accept_op("#");
            rn = as_int(expr());
            if (accept_op(","))
                flags = as_int(expr());
            emit(sfmt("mms_swap(%s, %s, %s);", n, rn, flags));
            return;
        }
        if (is_kw("CLOSE", 1)) {
            const char *n;

            if (is_kw("ALL", 2)) {
                cv.i += 3;
                emit("mms_close_all();");
                return;
            }
            cv.i += 2;
            accept_op("#");
            n = as_int(expr());
            emit(sfmt("mms_close(%s);", n));
            return;
        }
        if (is_kw("LOADARRAY", 1)) {
            /* LOADARRAY [#]n, w, h, array() - RGB888 colours,
               reduced by RGB121 bit extraction as the reference
               does.  An integer array; the reference also takes
               float, which nothing has needed yet. */
            const char *n, *w, *h;
            struct sym *sym;
            struct flat fl;

            cv.i += 2;
            accept_op("#");
            n = as_int(expr());
            expect_op(",");
            w = as_int(expr());
            expect_op(",");
            h = as_int(expr());
            expect_op(",");
            sym = arrayref(1);
            if (sym->ty != TY_I)
                cv_err("SPRITE LOADARRAY wants an integer array");
            fl = array_flat(sym);
            emit(sfmt("mms_loadarray(%s, %s, %s, %s, %s);",
                      n, w, h, fl.ptr, fl.cnt));
            return;
        }
        if (is_kw("LOAD", 1)) {
            /* LOAD file$ [,startsprite [,mode]] - bare commas
               allowed, as the reference's *argv[2] test allows. */
            const char *start = "1LL", *mode = "0LL";
            struct val v;

            cv.i += 2;
            v = expr();
            if (v.ty != TY_S)
                cv_err("SPRITE LOAD wants a file name");
            if (accept_op(",")) {
                if (!is_op(",", 0))
                    start = as_int(expr());
                if (accept_op(","))
                    mode = as_int(expr());
            }
            emit(sfmt("mms_load(%s, %s, %s);", v.code, start, mode));
            return;
        }
        if (is_kw("STATIC", 1)) {
            const char *n, *x, *y, *w, *h;

            if (is_kw("CLEAR", 2)) {
                cv.i += 3;
                emit("mms_static_clear();");
                return;
            }
            cv.i += 2;
            accept_op("#");
            n = as_int(expr());
            expect_op(",");
            if (is_kw("OFF", 0)) {
                cv.i += 1;
                emit(sfmt("mms_static(%s, 0, 0, 0, 0, 1);", n));
                return;
            }
            x = as_int(expr());
            expect_op(",");
            y = as_int(expr());
            expect_op(",");
            w = as_int(expr());
            expect_op(",");
            h = as_int(expr());
            emit(sfmt("mms_static(%s, %s, %s, %s, %s, 0);",
                      n, x, y, w, h));
            return;
        }
        if (is_kw("SCROLL", 1)) {
            /* SCROLL x, y [,colour] - the default is the reference's
               -2: wrap the departing band round. */
            const char *x, *y;
            const char *blank = "-2LL";

            cv.i += 2;
            x = as_int(expr());
            expect_op(",");
            y = as_int(expr());
            if (accept_op(","))
                blank = as_int(expr());
            emit(sfmt("mms_scroll(%s, %s, %s);", x, y, blank));
            return;
        }
        if (is_kw("SET", 1) && is_kw("TRANSPARENT", 2)) {
            const char *c;

            cv.i += 3;
            c = as_int(expr());
            emit(sfmt("mms_set_transparent(%s);", c));
            return;
        }
        if (is_kw("INTERRUPT", 1)) {
            cv.i += 2;
            cv.uses_interrupts = 1;
            emit(sfmt("mmi_sprite_int(%s);", int_handler()));
            return;
        }
        if (is_kw("NOINTERRUPT", 1)) {
            cv.i += 2;
            cv.uses_interrupts = 1;
            emit("mmi_sprite_noint();");
            return;
        }
        if (is_kw("STINTERRUPT", 1)) {
            cv.i += 2;
            cv.uses_interrupts = 1;
            emit(sfmt("mmi_st_int(%s);", int_handler()));
            return;
        }
        if (is_kw("NOSTINTERRUPT", 1)) {
            cv.i += 2;
            cv.uses_interrupts = 1;
            emit("mmi_st_noint();");
            return;
        }
        for (si = 0; nospr[si]; si++)
            if (is_kw(nospr[si], 1))
                cv_err(sfmt("SPRITE %s is not translated", nospr[si]));
        cv_err("unknown SPRITE form");
    }
    if (strcmp(up, "BLIT") == 0) {
        /* BLIT READ [#]n, x, y, w, h        screen -> buffer 1-64
           BLIT WRITE [#]n, x, y [, mode]    buffer -> screen, mode 0-7
           BLIT CLOSE [#]n                   free the buffer
           BLIT x1, y1, x2, y2, w, h         screen -> screen copy
           BLIT COMPRESSED addr, x, y [, t]  RLE 4bpp image from memory
           BLIT MEMORY addr, x, y [, t]      packed 4bpp, RLE if the
                                             size words carry the top bit

           BLIT FRAMEBUFFER s, d, x1, y1, x2, y2, w, h [, t]
                                             rectangle between N/F/L
           BLIT FLASH n, d, x1, y1, x2, y2, w, h [, t]
                                             image out of a slot

           cmd_blit (graphics/Blit.c), engine in mmb_blit.h.  WRITE's
           mode argument is optional WITHOUT the bare-comma licence the
           drawing commands have: the reference takes 5 or 7 arguments
           and nothing between (argc==6 is a syntax error).  The
           transparent colour is -1 (none) to 15, checked at run time
           as the reference's getint does.

           Not translated: LOAD (wants the BMP decoder), RESIZE, and
           the LCD-only MERGE / RGB332-only MEMORY332, which do not
           apply to these screen modes at all. */
        static const char *const noblit[] = {
            "LOAD", "RESIZE", "MERGE", "MEMORY332", NULL
        };
        int bi;

        cv.uses_blit = 1;
        if (is_kw("READ", 1)) {
            const char *n, *x, *y, *w, *h;
            cv.i += 2;
            accept_op("#");
            n = as_int(expr());
            expect_op(",");
            x = as_int(expr());
            expect_op(",");
            y = as_int(expr());
            expect_op(",");
            w = as_int(expr());
            expect_op(",");
            h = as_int(expr());
            emit(sfmt("mmb_blit_read(%s, %s, %s, %s, %s);",
                      n, x, y, w, h));
            return;
        }
        if (is_kw("WRITE", 1)) {
            const char *n, *x, *y;
            const char *mode = "0LL";
            cv.i += 2;
            accept_op("#");
            n = as_int(expr());
            expect_op(",");
            x = as_int(expr());
            expect_op(",");
            y = as_int(expr());
            if (accept_op(","))
                mode = as_int(expr());
            emit(sfmt("mmb_blit_write(%s, %s, %s, %s);", n, x, y, mode));
            return;
        }
        if (is_kw("CLOSE", 1)) {
            const char *n;
            cv.i += 2;
            accept_op("#");
            n = as_int(expr());
            emit(sfmt("mmb_blit_close(%s);", n));
            return;
        }
        if (is_kw("COMPRESSED", 1) || is_kw("MEMORY", 1)) {
            do_blit_memform();
            return;
        }
        if (is_kw("FRAMEBUFFER", 1) || is_kw("FLASH", 1)) {
            const char *src, *dst;
            const char *args[6];
            const char *blank = "-1LL";
            int is_flash = is_kw("FLASH", 1);
            int ai;

            cv.i += 2;
            if (is_flash) {
                cv.uses_flash = 1;
                src = as_int(expr());
                expect_op(",");
            } else {
                src = fb_buf();
                expect_op(",");
            }
            dst = fb_buf();
            for (ai = 0; ai < 6; ai++) {
                expect_op(",");
                args[ai] = as_int(expr());
            }
            if (accept_op(","))
                blank = as_int(expr());
            emit(sfmt("mmb_blit_%s(%s, %s, %s, %s, %s, %s, %s, %s, %s);",
                      is_flash ? "flash" : "fb", src, dst,
                      args[0], args[1], args[2], args[3], args[4],
                      args[5], blank));
            return;
        }
        for (bi = 0; noblit[bi]; bi++)
            if (is_kw(noblit[bi], 1))
                cv_err(sfmt("BLIT %s is not translated", noblit[bi]));
        {
            const char *x1, *y1, *x2, *y2, *w, *h;
            cv.i++;
            x1 = as_int(expr());
            expect_op(",");
            y1 = as_int(expr());
            expect_op(",");
            x2 = as_int(expr());
            expect_op(",");
            y2 = as_int(expr());
            expect_op(",");
            w = as_int(expr());
            expect_op(",");
            h = as_int(expr());
            emit(sfmt("mmb_blit_copy(%s, %s, %s, %s, %s, %s);",
                      x1, y1, x2, y2, w, h));
            return;
        }
    }
    if (strcmp(up, "FLASH") == 0) {
        /* FLASH DISK LOAD n, file$ [, O[VERWRITE]]
           FLASH ERASE n

           The image-slot half of MMBasic's FLASH command
           (FileIO.c:1232, :1039), against the pseudo slots of
           mmb_flash.h.  The program-management half - SAVE, LOAD,
           RUN, CHAIN, LIST - manages BASIC programs in flash, a
           thing this machine does with a filesystem, so it is
           refused by name rather than absorbed. */
        cv.uses_flash = 1;
        if (is_kw("DISK", 1) && is_kw("LOAD", 2)) {
            const char *n;
            const char *ovr = "0LL";
            struct val v;

            cv.i += 3;
            n = as_int(expr());
            expect_op(",");
            v = expr();
            if (v.ty != TY_S)
                cv_err("FLASH DISK LOAD wants a file name");
            if (accept_op(",")) {
                if (accept_kw("O") || accept_kw("OVERWRITE"))
                    ovr = "1LL";
                else
                    cv_err("FLASH DISK LOAD takes only O here");
            }
            emit(sfmt("mmf_disk_load(%s, %s, %s);", v.code, n, ovr));
            return;
        }
        if (is_kw("ERASE", 1)) {
            const char *n;

            cv.i += 2;
            n = as_int(expr());
            emit(sfmt("mmf_erase(%s);", n));
            return;
        }
        cv_err("only FLASH DISK LOAD and FLASH ERASE are translated");
    }
    if (strcmp(up, "PLAY") == 0) {
        /* PLAY MP3 f$          play a file, in the BACKGROUND
           PLAY VOLUME n        0-100, remembered for later PLAYs
           PLAY STOP            stop whatever is playing

           MMBasic's PLAY VOLUME takes a level per channel; this takes
           one, because the volume reaches playmp3 as an argument and
           playmp3 applies it to both.  Left and right separately would
           mean a second argument that does nothing yet, which is worse
           than not offering it.

           MP3 does NOT wait.  playmp3 is a separate process feeding the
           kernel's ring, so the BASIC program carries on while the
           music plays - the thing MMBasic needs checkWAVinput() in its
           interpreter loop to manage, and which costs us nothing.  That
           also means mm_run_exec cannot be used: it waits. */
        /* STOP is first because MMBasic's cmd_play tests it first,
           before it even checks that audio is configured: stopping what
           is not playing is never an error.  It carries no volume, so
           it does not set uses_play - a program whose only PLAY is a
           STOP would then declare a variable it never reads. */
        if (is_kw("STOP", 1)) {
            cv.i += 2;
            emit("mm_play_stop();");
            return;
        }
        cv.uses_play = 1;
        if (is_kw("VOLUME", 1)) {
            struct val v;
            cv.i += 2;
            v = expr();
            if (v.ty == TY_S)
                cv_err("PLAY VOLUME wants a number");
            if (cv.uses_playd) {
                /* a running daemon hears the change at once */
                emit(sfmt("mmp_volume(%s);", as_int(v)));
            } else {
                emit(sfmt("mm_play_volume = (int)(%s);", v.code));
                emit("if (mm_play_volume < 0) mm_play_volume = 0;");
                emit("if (mm_play_volume > 100) "
                     "mm_play_volume = 100;");
            }
            return;
        }
        if (is_kw("SOUND", 1)) {
            /* PLAY SOUND voice, channel, type [, freq [, vol]]
               channel: L R B M (M means both, as the reference
               takes it); type: O S Q T W P N - U (a user table) is
               not translated.  cmd_play at Audio.c:1946. */
            static const struct kwval chans[] = {
                { "L", 1 }, { "R", 2 }, { "B", 3 }, { "M", 3 },
                { NULL, 0 }
            };
            static const struct kwval types[] = {
                { "O", 0 }, { "S", 1 }, { "Q", 2 }, { "T", 3 },
                { "W", 4 }, { "P", 5 }, { "N", 6 }, { NULL, 0 }
            };
            const char *n, *sides, *ty;
            const char *freq = "10.0", *vol = "25LL";

            cv.uses_playd = 1;
            cv.i += 2;
            n = as_int(expr());
            expect_op(",");
            /* Both of these are a bare letter, a quoted one or a
               string the program works out - MMBasic's cmd_play takes
               all three, and picofrog writes them quoted and in lower
               case. */
            sides = kw_or_str(chans, "mmp_side",
                              "PLAY SOUND wants a channel: L, R, B or M");
            expect_op(",");
            if (is_kw("U", 0))
                cv_err("PLAY SOUND type U is not translated");
            ty = kw_or_str(types, "mmp_type",
                           "PLAY SOUND wants a type: O S Q T W P or N");
            if (accept_op(",")) {
                if (!is_op(",", 0))
                    freq = as_flt(expr());
                if (accept_op(","))
                    vol = as_int(expr());
            }
            emit(sfmt("mmp_sound(%s, %s, %s, %s, %s);",
                      n, sides, ty, freq, vol));
            return;
        }
        if (is_kw("TONE", 1)) {
            /* PLAY TONE left, right [, dur_ms [, interrupt]] - no
               duration means until PLAY STOP; the completion
               interrupt is a deadline here, not an IPC. */
            const char *fl, *fr;
            const char *dur = "0.0", *fn = NULL;

            cv.uses_playd = 1;
            cv.i += 2;
            fl = as_flt(expr());
            expect_op(",");
            fr = as_flt(expr());
            if (accept_op(",")) {
                if (!is_op(",", 0))
                    dur = as_flt(expr());
                if (accept_op(",")) {
                    cv.uses_interrupts = 1;
                    fn = int_handler();
                }
            }
            if (fn != NULL) {
                emit(sfmt("mmi_tone_int(%s);", fn));
                emit(sfmt("mmp_tone(%s, %s, %s, 1);", fl, fr, dur));
            } else {
                emit(sfmt("mmp_tone(%s, %s, %s, 0);", fl, fr, dur));
            }
            return;
        }
        if (is_kw("MODFILE", 1)) {
            /* PLAY MODFILE f$ [, interrupt] - with an interrupt the
               song plays once and the player's exit fires it; without
               one it loops until PLAY STOP. */
            const char *fn = NULL;
            struct val v;

            cv.uses_playd = 1;
            cv.i += 2;
            v = expr();
            if (v.ty != TY_S)
                cv_err("PLAY MODFILE wants a file name");
            if (accept_op(",")) {
                cv.uses_interrupts = 1;
                fn = int_handler();
            }
            if (fn != NULL) {
                emit(sfmt("mmi_mod_int(%s);", fn));
                emit(sfmt("mmp_modfile(%s, 1);", v.code));
            } else {
                emit(sfmt("mmp_modfile(%s, 0);", v.code));
            }
            return;
        }
        if (is_kw("MODSAMPLE", 1)) {
            /* PLAY MODSAMPLE sample, channel [, volume] - a request
               to the RUNNING player to mix a sample the file already
               holds over the music. */
            const char *sm, *ch;
            const char *vol = "64LL";

            cv.uses_playd = 1;
            cv.i += 2;
            sm = as_int(expr());
            expect_op(",");
            ch = as_int(expr());
            if (accept_op(","))
                vol = as_int(expr());
            emit(sfmt("mmp_modsample(%s, %s, %s);", sm, ch, vol));
            return;
        }
        /* MP3, WAV and FLAC are the same statement with a different
           program behind it: each spawns a one-shot player that holds
           the PCM stream for its own lifetime, so unlike SOUND and
           MODFILE there is no daemon to command and NO KIND to record -
           mmp_adopt says as much ("an MP3 player writes no kind file"),
           and PLAY STOP reaches all three the same way, by signalling
           whoever owns the stream. */
        {
            static const char *const files[3][2] = {
                { "MP3", "playmp3" },
                { "WAV", "playwav" },
                { "FLAC", "playflac" }
            };
            int k;

            for (k = 0; k < 3; k++) {
                if (is_kw(files[k][0], 1)) {
                    struct val v;
                    cv.i += 2;
                    v = expr();
                    if (v.ty != TY_S)
                        cv_err(sfmt("PLAY %s wants a file name",
                                    files[k][0]));
                    emit("mm_run_begin();");
                    emit(sfmt("mm_run_arg(%s);",
                              c_string_literal(files[k][1])));
                    emit(sfmt("mm_run_arg(%s);", v.code));
                    emit("mm_run_arg_i(mm_play_volume);");
                    emit("mm_play_start();");
                    return;
                }
            }
        }
        cv_err("only PLAY MP3, WAV, FLAC, MODFILE, MODSAMPLE, SOUND, TONE, VOLUME and STOP are translated");
    }
    if (strcmp(up, "CIRCLE") == 0) {
        /* CIRCLE x, y, r [, lw [, aspect [, colour [, fill]]]]
           The geometry is mmb_gfx_circle.h's, not the runtime's.  MMBasic
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
        cv.uses_circle = 1;
        emit(sfmt("mmg_circle(%s, %s, %s, %s, %s, %s, %s);",
                  x, y, r, lw, col, fill, asp));
        return;
    }
    if (strcmp(up, "BOX") == 0 || strcmp(up, "RBOX") == 0) {
        /* BOX  x, y, w, h [, lw     [, colour [, fill]]]
           RBOX x, y, w, h [, radius [, colour [, fill]]]

           cmd_box / cmd_rbox: width and height may be negative and the
           box is drawn the other way; the line width (or the corner
           radius) defaults to 1 (or 10); the colours default to the
           current foreground and to no fill.  A bare comma is legal in
           every optional position, as everywhere. */
        int is_rbox = (strcmp(up, "RBOX") == 0);
        const char *x, *y, *w, *h;
        const char *lw, *col = "MM_CUR", *fill = "MM_CUR";

        lw = is_rbox ? "10LL" : "1LL";
        cv.i++;
        x = as_int(expr());
        expect_op(",");
        y = as_int(expr());
        expect_op(",");
        w = as_int(expr());
        expect_op(",");
        h = as_int(expr());
        if (accept_op(",")) {
            if (!is_op(",", 0))
                lw = as_int(expr());
            if (accept_op(",")) {
                if (!is_op(",", 0))
                    col = as_int(expr());
                if (accept_op(","))
                    fill = as_int(expr());
            }
        }
        if (is_rbox) {
            cv.uses_rbox = 1;
            emit(sfmt("mmg_rbox(%s, %s, %s, %s, %s, %s, %s);",
                      x, y, w, h, lw, col, fill));
        } else {
            cv.uses_box = 1;
            emit(sfmt("mmg_box(%s, %s, %s, %s, %s, %s, %s);",
                      x, y, w, h, lw, col, fill));
        }
        return;
    }
    if (strcmp(up, "GUI") == 0 && is_kw("BITMAP", 1)) {
        /* GUI BITMAP x, y, bits [,w] [,h] [,scale] [,c] [,bc]
         *
         * The one form of GUI that means anything here: the rest of
         * cmd_gui is touch-screen widgets, and this machine has no
         * touch hardware.  MMBasic's own defaults (Draw.c:449) are 8x8
         * at scale 1 in the current colours - note the SCALE default is
         * 1 and not the FONT scale, whatever the manual says; the code
         * never reads the font.
         *
         * `bits` may be a string or an integer, and the two are
         * different byte sources rather than the same one converted -
         * see mmb_gui.h. */
        const char *x, *y;
        const char *w = "8LL", *h = "8LL", *scale = "1LL";
        const char *fc = "mm_fg()", *bc = "mm_bg()";
        struct val v;
        int k;

        cv.i += 2;
        cv.uses_gui = 1;
        x = as_int(expr());
        expect_op(",");
        y = as_int(expr());
        expect_op(",");
        v = expr();
        for (k = 0; k < 5; k++) {
            const char *e;
            if (!accept_op(","))
                break;
            if (is_op(",", 0))
                continue;               /* a bare comma keeps the default */
            e = as_int(expr());
            if (k == 0) w = e;
            else if (k == 1) h = e;
            else if (k == 2) scale = e;
            else if (k == 3) fc = e;
            else bc = e;
        }
        if (v.ty == TY_S)
            emit(sfmt("mmg_gui_bitmap(%s, %s, (const unsigned char *)"
                      "mm_cstr(%s), mm_slen(%s), %s, %s, %s, %s, %s);",
                      x, y, v.code, v.code, w, h, scale, fc, bc));
        else if (v.ty == TY_I)
            emit(sfmt("mmg_gui_bitmap_i(%s, %s, %s, %s, %s, %s, %s, %s);",
                      x, y, v.code, w, h, scale, fc, bc));
        else
            cv_err("GUI BITMAP wants a string or an integer");
        return;
    }
    if (strcmp(up, "TRIANGLE") == 0) {
        /* TRIANGLE x1, y1, x2, y2, x3, y3 [, colour [, fill]]

           SAVE and RESTORE need the interpreter's blit buffers, so
           only the drawing form is translated.  The colour may be a
           bare comma, as everywhere. */
        const char *x1, *y1, *x2, *y2, *x3, *y3;
        const char *col = "MM_CUR", *fill = "MM_CUR";

        cv.i++;
        if (accept_kw("SAVE") || accept_kw("RESTORE")) {
            cv_err("only the drawing form of TRIANGLE is translated");
            return;
        }
        x1 = as_int(expr());
        expect_op(",");
        y1 = as_int(expr());
        expect_op(",");
        x2 = as_int(expr());
        expect_op(",");
        y2 = as_int(expr());
        expect_op(",");
        x3 = as_int(expr());
        expect_op(",");
        y3 = as_int(expr());
        if (accept_op(",")) {
            if (!is_op(",", 0))
                col = as_int(expr());
            if (accept_op(","))
                fill = as_int(expr());
        }
        cv.uses_triangle = 1;
        emit(sfmt("mmg_triangle(%s, %s, %s, %s, %s, %s, %s, %s);",
                  x1, y1, x2, y2, x3, y3, col, fill));
        return;
    }
    if (strcmp(up, "POLYGON") == 0) {
        /* POLYGON n, xarray(), yarray() [, bordercolour [, fillcolour]]

           Always closed - cmd_polygon passes close=1 to polygon(); the
           open form belongs to an internal GUI caller.  n == 0 means
           "as many as the array holds", which is MMBasic's xcount == 0.

           The multi-polygon form, where the first argument is an ARRAY
           of vertex counts and the coordinate arrays hold several
           shapes end to end, is refused by name rather than
           half-drawn. */
        struct sym *xsym, *ysym;
        struct flat xfl, yfl;
        const char *nverts, *col = "MM_CUR", *fill = "MM_CUR";
        const char *xfp, *xip, *yfp, *yip;
        const char *counts[2];

        cv.i++;
        if (is_array_arg()) {
            cv_err("the multi-polygon form of POLYGON (a vertex count "
                   "array) is not translated; pass a count and one "
                   "polygon's points");
            return;
        }
        nverts = as_int(expr());
        expect_op(",");
        xsym = arrayref(1);
        xfl = array_flat(xsym);
        expect_op(",");
        ysym = arrayref(1);
        yfl = array_flat(ysym);
        if (xsym->ty == TY_S)
            cv_err("POLYGON needs numeric coordinate arrays, and '%s' "
                   "is a string array", xsym->name);
        if (ysym->ty == TY_S)
            cv_err("POLYGON needs numeric coordinate arrays, and '%s' "
                   "is a string array", ysym->name);
        if (accept_op(",")) {
            if (!is_op(",", 0))
                col = as_int(expr());
            if (accept_op(","))
                fill = as_int(expr());
        }
        if (xsym->ty == TY_F) { xfp = xfl.ptr; xip = "NULL"; }
        else                  { xfp = "NULL"; xip = xfl.ptr; }
        if (ysym->ty == TY_F) { yfp = yfl.ptr; yip = "NULL"; }
        else                  { yfp = "NULL"; yip = yfl.ptr; }
        counts[0] = xfl.cnt;
        counts[1] = yfl.cnt;
        cv.uses_polygon = 1;
        emit(sfmt("mmg_polygon(%s, %s, %s, %s, %s, %s, %s, %s);",
                  xfp, xip, yfp, yip, nverts, shortest(counts, 2),
                  col, fill));
        return;
    }
    if (strcmp(up, "FILL") == 0) {
        /* FILL x, y, colour [, boundary]

           With a boundary the fill stops at that colour; without one
           it replaces the colour at the starting point.  MM_CUR
           carries "no boundary given" to the header, as everywhere. */
        const char *x, *y, *col, *bound = "MM_CUR";

        cv.i++;
        x = as_int(expr());
        expect_op(",");
        y = as_int(expr());
        expect_op(",");
        col = as_int(expr());
        if (accept_op(","))
            bound = as_int(expr());
        cv.uses_fill = 1;
        emit(sfmt("mmg_fill(%s, %s, %s, %s);", x, y, col, bound));
        return;
    }
    if (strcmp(up, "BEZIER") == 0) {
        /* BEZIER xarray(), yarray() [, n] [, colour]

           INTEGER arrays, which is MMBasic's own restriction -
           cmd_bezier reads them with parseintegerarray.  A float array
           is refused rather than converted, because it is a program
           that would only work here. */
        struct sym *xsym, *ysym;
        struct flat xfl, yfl;
        const char *npts = "0LL", *col = "MM_CUR";
        const char *counts[2];

        cv.i++;
        xsym = arrayref(1);
        xfl = array_flat(xsym);
        expect_op(",");
        ysym = arrayref(1);
        yfl = array_flat(ysym);
        if (xsym->ty != TY_I)
            cv_err("BEZIER needs INTEGER control point arrays, and '%s' "
                   "is not one", xsym->name);
        if (ysym->ty != TY_I)
            cv_err("BEZIER needs INTEGER control point arrays, and '%s' "
                   "is not one", ysym->name);
        if (accept_op(",")) {
            if (!is_op(",", 0))
                npts = as_int(expr());
            if (accept_op(","))
                col = as_int(expr());
        }
        counts[0] = xfl.cnt;
        counts[1] = yfl.cnt;
        cv.uses_bezier = 1;
        emit(sfmt("mmg_bezier(%s, %s, %s, %s, %s);",
                  xfl.ptr, yfl.ptr, npts, shortest(counts, 2), col));
        return;
    }
    if (strcmp(up, "ARC") == 0) {
        /* ARC x, y, r1 [, r2], rad1, rad2 [, colour]

           An omitted r2 - a bare comma - is a one pixel wide arc at
           r1, which cmd_arc expresses as r2 = r1, r1 - 1; MM_CUR
           carries the omission to the header.  The angles are
           MMBasic's compass degrees: 0 up, clockwise. */
        const char *x, *y, *r1, *a1, *a2;
        const char *r2 = "MM_CUR", *col = "MM_CUR";

        cv.i++;
        x = as_int(expr());
        expect_op(",");
        y = as_int(expr());
        expect_op(",");
        r1 = as_int(expr());
        expect_op(",");
        if (!is_op(",", 0))
            r2 = as_int(expr());
        expect_op(",");
        a1 = as_int(expr());
        expect_op(",");
        a2 = as_int(expr());
        if (accept_op(","))
            col = as_int(expr());
        cv.uses_arc = 1;
        emit(sfmt("mmg_arc(%s, %s, %s, %s, %s, %s, %s);",
                  x, y, r1, r2, a1, a2, col));
        return;
    }
    if (strcmp(up, "TEXT") == 0) {
        /* TEXT x, y, string$ [, alignment$] [, font] [, scale]
                              [, colour] [, background]

           Every argument after the string is optional and a bare comma
           is legal in any of them, as everywhere in MMBasic.  The two
           colours default to COLOUR's - resolved HERE, by emitting
           mm_fg()/mm_bg(), because -1 is a colour TEXT accepts
           (transparent paper) and so cannot double as the "none given"
           sentinel the other statements use. */
        const char *x, *y, *s;
        /* 0 for an omitted font or scale, NOT 1: the default is the
           CURRENT font and scale, which is what FONT set, and only the
           runtime knows them.  Draw.c:2133 cmd_text takes both from
           gui_font.  Emitting 1 here meant a program that said FONT 10
           and then drew with the plain four-argument TEXT - which is
           what MMBasic programs do - got font 1 every time, so an 8x8
           panel came out in 8x12 and overlapped, and DefineFont looked
           broken when it was not. */
        const char *just = "0", *font = "0LL", *scale = "0LL";
        const char *fc = "mm_fg()", *bc = "mm_bg()";

        cv.i++;
        x = as_int(expr());
        expect_op(",");
        y = as_int(expr());
        expect_op(",");
        s = as_str(expr());
        if (accept_op(",")) {
            if (!is_op(",", 0))
                just = just_arg();
            if (accept_op(",")) {
                if (!is_op(",", 0))
                    font = as_int(expr());
                if (accept_op(",")) {
                    if (!is_op(",", 0))
                        scale = as_int(expr());
                    if (accept_op(",")) {
                        if (!is_op(",", 0))
                            fc = as_int(expr());
                        if (accept_op(","))
                            bc = as_int(expr());
                    }
                }
            }
        }
        cv.uses_text = 1;
        emit(sfmt("mmg_text(%s, %s, %s, %s, %s, %s, %s, %s);",
                  x, y, s, just, font, scale, fc, bc));
        return;
    }
    if (strcmp(up, "RTC") == 0
        && (is_kw("GETREG", 1) || is_kw("SETREG", 1))) {
        /* RTC GETREG reg, var
           RTC SETREG reg, value

           MMBasic's own pair (I2C.c cmd_rtc), and the way an alarm is
           armed there - it has no alarm command.  Write the match time
           into 0x07-0x0A, then INTCN|A1IE into 0x0E, and the chip pulls
           GP32 low when the time comes. */
        int get;
        const char *reg;

        cv.i++;
        get = accept_kw("GETREG");
        if (!get)
            accept_kw("SETREG");
        reg = as_int(expr());
        expect_op(",");
        if (get) {
            const char *tgt = lvalue_from_here();
            emit(sfmt("%s = mm_rtcreg(%s, 0, 0);", tgt, reg));
        } else {
            emit(sfmt("mm_rtcreg(%s, %s, 1);", reg, as_int(expr())));
        }
        return;
    }
    if (strcmp(up, "WEB") == 0) {
        cv.i++;
        do_web();
        return;
    }
    if (strcmp(up, "ONEWIRE") == 0) {
        cv.i++;
        do_onewire();
        return;
    }
    if (strcmp(up, "TEMPR") == 0 && is_kw("START", 1)) {
        /* TEMPR START pin [, precision [, timeout]] - begin a
           conversion and come back for it later.  The reading form is a
           function. */
        const char *pin, *prec = "1", *tmo = "-1";

        cv.i += 2;
        pin = as_int(expr());
        if (accept_op(",")) {
            if (!is_op(",", 0) && !stmt_end())
                prec = as_int(expr());
            if (accept_op(","))
                tmo = as_int(expr());
        }
        cv.uses_gpio = 1;
        cv.uses_onewire = 1;
        emit(sfmt("mmow_tempr_start(%s, %s, %s);", pin, prec, tmo));
        return;
    }
    if (strcmp(up, "SPI") == 0 && !is_op("(", 1)) {
        /* SPI( is the function - write a unit and read one back - and
           it is handled in the expression parser.  A statement starting
           with SPI is the command. */
        do_spi();
        return;
    }
    if (strcmp(up, "I2C2") == 0) {
        do_i2c2();
        return;
    }
    if (strcmp(up, "I2C") == 0) {
        do_i2c0();
        return;
    }
    if (strcmp(up, "SETTICK") == 0) {
        do_settick();
        return;
    }
    if (strcmp(up, "PWM") == 0) {
        /* PWM slice, frequency, duty1 [, duty2]
           PWM slice, OFF

           A SLICE, not a pin: one slice drives two pins, and SETPIN
           pin, PWM is what attaches a pin to it.  MMBasic is the same
           and for the same reason. */
        const char *sl, *freq, *d1, *d2, *have2;

        cv.i++;
        cv.uses_pwm = 1;
        sl = as_int(expr());
        expect_op(",");
        if (accept_kw("OFF")) {
            emit(sfmt("mmp_pwm_off(%s);", sl));
            return;
        }
        freq = as_flt(expr());
        expect_op(",");
        d1 = as_flt(expr());
        /* Channel B is optional and OMITTING IT LEAVES IT ALONE - two
           outputs share a slice, so setting one must not stop the
           other.  The flag says whether it was given; there is no spare
           value to use as a sentinel, because a negative duty already
           means inverted. */
        if (accept_op(",")) {
            d2 = as_flt(expr());
            have2 = "1";
        } else {
            d2 = "0.0";
            have2 = "0";
        }
        emit(sfmt("mmp_pwm2(%s, %s, %s, %s, %s);", sl, freq, d1, d2,
                  have2));
        return;
    }
    if (strcmp(up, "SERVO") == 0) {
        /* SERVO slice, position1 [, position2]
           SERVO slice, OFF

           PWM at a 50Hz frame with the position as a pulse width;
           MMBasic's mapping is duty = 5 + position * 0.05, so 0 is 1ms,
           50 is 1.5ms and 100 is 2ms. */
        const char *sl, *p1, *p2, *have2;

        cv.i++;
        cv.uses_pwm = 1;
        sl = as_int(expr());
        expect_op(",");
        if (accept_kw("OFF")) {
            emit(sfmt("mmp_pwm_off(%s);", sl));
            return;
        }
        p1 = as_flt(expr());
        if (accept_op(",")) {
            p2 = as_flt(expr());
            have2 = "1";
        } else {
            p2 = "0.0";
            have2 = "0";
        }
        emit(sfmt("mms_servo(%s, %s, %s, %s);", sl, p1, p2, have2));
        return;
    }
    if (strcmp(up, "SETPIN") == 0) {
        /* SETPIN pin, DIN|DOUT

           The pin is the GPIO number, not MMBasic's connector-pin
           numbering: the GPIO number is what the PC3 schematic, the
           kernel and every other tool on this machine use, and a
           second numbering for one statement would confuse more than
           the incompatibility does. */
        const char *pin;
        const char *mode;

        cv.i++;
        pin = as_int(expr());
        expect_op(",");
        if (accept_kw("DOUT"))
            mode = "MMG_PIN_DOUT";
        else if (accept_kw("DIN"))
            mode = "MMG_PIN_DIN";
        else if (accept_kw("AIN"))
            mode = "MMG_PIN_AIN";
        else if (accept_kw("ARAW"))
            mode = "MMG_PIN_ARAW";
        else if (accept_kw("OFF"))
            mode = "MMG_PIN_OFF";
        else if (accept_kw("INTH"))
            mode = "MMG_PIN_INTH";
        else if (accept_kw("INTL"))
            mode = "MMG_PIN_INTL";
        else if (accept_kw("INTB"))
            mode = "MMG_PIN_INTB";
        else if (accept_kw("PWM")) {
            mode = "MMG_PIN_PWM";
            cv.uses_pwm = 1;
        }
        else {
            /* SETPIN sda, scl, I2C2   - the pin-PAIR form
               SETPIN p1, p2, p3, SPI  - the pin-TRIPLE form
               Reached here because what followed the comma was not a
               mode word but another pin. */
            const char *p2 = as_int(expr());
            const char *p3;

            expect_op(",");
            if (accept_kw("I2C2")) {
                cv.uses_i2c = 1;
                emit(sfmt("__mmi2c_sda = %s; __mmi2c_scl = %s;", pin, p2));
                return;
            }
            p3 = as_int(expr());
            expect_op(",");
            if (!accept_kw("SPI")) {
                cv_err("SETPIN takes DIN, DOUT, AIN, ARAW, "
                       "INTH, INTL, INTB, PWM or OFF, or a pin pair "
                       "followed by I2C2, or a pin triple followed by "
                       "SPI");
                return;
            }
            cv.uses_spi = 1;
            /* Any order: which signal each pin carries is decided by
               the pin number, not by its position here, exactly as
               MMBasic works it out from PinDef[pin].mode rather than
               from the order written.  mmb_spi.h sorts them. */
            emit(sfmt("__mmspi_a = %s; __mmspi_b = %s; __mmspi_c = %s;",
                      pin, p2, p3));
            return;
        }
        cv.uses_gpio = 1;
        if (strncmp(mode, "MMG_PIN_INT", 11) == 0) {
            /* SETPIN pin, INTH|INTL|INTB, handler [, PULLUP|PULLDOWN] */
            const char *fn;
            expect_op(",");
            cv.uses_interrupts = 1;
            fn = int_handler();
            if (!fn)
                return;
            emit(sfmt("mmi_setpin_int(%s, %s, %s, %s);", pin, mode, fn,
                      setpin_pull()));
            return;
        }
        if (strcmp(mode, "MMG_PIN_OFF") == 0 && cv.uses_interrupts) {
            /* OFF has to disarm an interrupt as well as reset the pin.
               Only a program that arms one carries this. */
            emit(sfmt("mmi_setpin_off(%s);", pin));
            return;
        }
        /* SETPIN pin, DIN [, PULLUP|PULLDOWN].  MMBasic allows the
           option on the input modes only; the others take no third
           argument and one is refused rather than ignored. */
        emit(sfmt("mmg_setpin(%s, %s, %s);", pin, mode,
                  strcmp(mode, "MMG_PIN_DIN") == 0 ? setpin_pull() : "0"));
        return;
    }
    if (strcmp(up, "PIN") == 0 && is_op("(", 1)) {
        /* PIN(n) = value.  The reading form is a function, handled in
           the expression parser; a statement starting with PIN can
           only be the assignment. */
        const char *pin, *val;

        cv.i++;
        expect_op("(");
        pin = as_int(expr());
        expect_op(")");
        expect_op("=");
        val = as_int(expr());
        cv.uses_gpio = 1;
        emit(sfmt("mmg_pin_put(%s, %s);", pin, val));
        return;
    }
    if ((strcmp(up, "BIT") == 0 || strcmp(up, "BYTE") == 0)
        && is_op("(", 1)) {
        /* BIT(intvar, n) = 0|1        set or clear one bit
           BYTE(strvar$, n) = 0..255   overwrite one character

           Both reach INTO a variable rather than replacing it, so the
           target is an lvalue and not an expression - MMBasic calls
           findvar and refuses a constant.  The TYPE check is done here
           rather than at run time: the translator knows what an lvalue
           is when it generates the call, so a BIT on a string is a
           translation error naming the line, which is better than
           MMBasic managing "Not an integer" at run time. */
        struct tok *t2;
        struct sym *s;
        const char *tgt, *n, *v;
        int isbit = strcmp(up, "BIT") == 0;

        cv.i++;
        expect_op("(");
        /* lvalue_from_here, opened up: it returns the accessor and
           drops the symbol, and the symbol is what carries the type.
           Asking reference() a second time would register an implied
           global twice. */
        t2 = nxt();
        if (t2->kind != T_ID)
            cv_err("%s() assignment needs a variable", up);
        s = reference(t2->text, is_op("(", 0));
        tgt = s->is_array ? index_of(s) : s->acc;
        if (isbit && s->ty != TY_I)
            cv_err("BIT() assignment needs an integer variable");
        if (!isbit && s->ty != TY_S)
            cv_err("BYTE() assignment needs a string variable");
        expect_op(",");
        n = as_int(expr());
        expect_op(")");
        expect_op("=");
        v = as_int(expr());
        cv.uses_misc = 1;
        if (isbit)
            emit(sfmt("mm_bit_assign(&(%s), %s, %s);", tgt, n, v));
        else
            emit(sfmt("mm_byte_assign(%s, %s, %s);", tgt, n, v));
        return;
    }
    if (strcmp(up, "FLAG") == 0 && is_op("(", 1)) {
        /* FLAG(n) = 0|1 - one of the sixty-four scratch bits.  The
           reading form is a function; a statement can only assign. */
        const char *n, *v;

        cv.i++;
        expect_op("(");
        n = as_int(expr());
        expect_op(")");
        expect_op("=");
        v = as_int(expr());
        cv.uses_misc = 1;
        emit(sfmt("mm_flag_assign(%s, %s);", n, v));
        return;
    }
    if (strcmp(up, "FLAGS") == 0 && is_op("=", 1)) {
        /* FLAGS = value - all sixty-four at once.  Reading them is
           MM.INFO(FLAGS), which is where MMBasic put it. */
        cv.i += 2;
        cv.uses_misc = 1;
        emit(sfmt("mm_flags_set(%s);", as_int(expr())));
        return;
    }
    if (strcmp(up, "LMID") == 0 && is_op("(", 1)) {
        /* LMID(a(), start [, num]) = s$

           A SPLICE, not an overwrite: num bytes come out and the string
           goes in, so the long string changes length unless the two
           match.  Leaving num out means "as many as the replacement
           has" - see mm_ls_lmid. */
        struct flat f;
        struct val start, num, v;
        int has_num = 0;

        cv.i++;
        expect_op("(");
        f = lsref();
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
            cv_err("LMID() assignment needs a string");
        cv.uses_lstring = 1;
        emit(sfmt("mm_ls_lmid(%s, %s, %s, %s, %s);", f.ptr, f.cnt,
                  as_int(start), has_num ? as_int(num) : "-1LL", v.code));
        return;
    }
    if (strcmp(up, "PORT") == 0 && is_op("(", 1)) {
        /* PORT(pin, nbits [, pin, nbits]...) = value

           Several output pins written as one number, every pin moving
           on the same edge - see mmb_port.h for the bit order and for
           why a loop over PIN() is not the same thing.  The pairs are
           written into the runtime's table one call each, because FCC
           has no compound literals and a pin number can be an
           expression, so the static-table trick used for array bounds
           does not apply either. */
        const char *pins[8], *bits[8], *val;
        int n = 0, k;

        cv.i++;
        expect_op("(");
        for (;;) {
            if (n >= 8)
                cv_err("PORT takes at most 8 pin groups");
            pins[n] = as_int(expr());
            expect_op(",");
            bits[n] = as_int(expr());
            n++;
            if (!accept_op(","))
                break;
        }
        expect_op(")");
        expect_op("=");
        val = as_int(expr());
        cv.uses_gpio = 1;
        cv.uses_port = 1;
        for (k = 0; k < n; k++)
            emit(sfmt("mmg_port_group(%d, %s, %s);", k, pins[k], bits[k]));
        emit(sfmt("mmg_port_put(%d, %s);", n, val));
        return;
    }
    if (strcmp(up, "MAP") == 0) {
        /* MAP(n) = colour     collect one entry
           MAP SET             apply the collected palette
           MAP RESET           back to the mode's own
           MAP MAXIMITE        the Colour Maximite's sixteen
           MAP GRAYSCALE       sixteen greys (GREYSCALE too)

           The function form MAP(n) is handled in the expression
           parser; only the statement form can be followed by '='. */
        const char *n, *c;

        cv.i++;
        if (accept_kw("SET")) {
            emit("mm_map_set();");
            return;
        }
        if (accept_kw("RESET")) {
            emit("mm_map_reset();");
            return;
        }
        if (accept_kw("MAXIMITE")) {
            cv.uses_mappal = 1;
            emit("mmg_map_maximite();");
            return;
        }
        if (accept_kw("GRAYSCALE") || accept_kw("GREYSCALE")) {
            cv.uses_mappal = 1;
            emit("mmg_map_greyscale();");
            return;
        }
        expect_op("(");
        n = as_int(expr());
        expect_op(")");
        expect_op("=");
        c = as_int(expr());
        emit(sfmt("mm_map(%s, %s);", n, c));
        return;
    }
    if (strcmp(up, "FONT") == 0) {
        /* FONT [#]n [, scale] - the font PRINT draws in.  MMBasic
           allows the # and ignores it, as it does on file numbers. */
        const char *n;
        const char *scale = "1LL";

        cv.i++;
        accept_op("#");
        n = as_int(expr());
        if (accept_op(","))
            scale = as_int(expr());
        emit(sfmt("mm_font(%s, %s);", n, scale));
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
        if (is_kw("MAP", 0)) {
            /* COLOUR MAP in%(), out%() [, map%()] - a whole array of
               colour codes 0-15 turned into RGB888.  The array form of
               MAP(), and it shares mm_map_get with it, so the default
               palette can only be described in one place.

               Integer arrays only.  MMBasic's parsenumberarray takes
               float ones too, but every spelling in its manual is % and
               a float palette would double the runtime for no program
               that exists. */
            struct sym *src, *dst;
            struct flat sf, df;
            const char *cmap = "NULL", *cmapn = "0";

            cv.i++;
            src = arrayref(1);
            expect_op(",");
            dst = arrayref(1);
            if (accept_op(",")) {
                struct sym *m = arrayref(1);
                struct flat mf;

                if (m->ty != TY_I)
                    cv_err("COLOUR MAP's palette must be an integer "
                           "array");
                mf = array_line(m);
                cmap = mf.ptr;
                cmapn = mf.cnt;
            }
            if (src->ty != TY_I || dst->ty != TY_I)
                cv_err("COLOUR MAP works on integer arrays");
            sf = array_flat(src);
            df = array_flat(dst);
            cv.uses_misc = 1;
            emit(sfmt("mm_colour_map(%s, %s, %s, %s, %s, %s);",
                      sf.ptr, sf.cnt, df.ptr, df.cnt, cmap, cmapn));
            return;
        }
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
           PIXEL xa(), ya() [, c | ca()]   - a whole run of points

           The function form PIXEL(x,y) is handled in the expression
           parser; a statement never starts with the open bracket. */
        struct val x, y;
        const char *col;
        const char *xs, *ys;

        cv.i++;
        if (is_array_arg()) {
            do_pixels();
            return;
        }
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
        const char *col = "MM_CUR", *wid = NULL;
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
                    wid = as_int(w);
            }
            if (accept_op(","))
                col = as_int(expr());
        }
        a = as_int(x1);
        b = as_int(y1);
        c2 = as_int(x2);
        d = as_int(y2);
        if (wid == NULL) {
            emit(sfmt("mm_line(%s, %s, %s, %s, %s);", a, b, c2, d, col));
        } else {
            /* A width is four different algorithms in the firmware,
               picked by shape - see mmb_gfx_line.h.  Only a program
               that asks for one carries them. */
            cv.uses_linew = 1;
            emit(sfmt("mmg_linew(%s, %s, %s, %s, %s, %s);",
                      a, b, c2, d, wid, col));
        }
        return;
    }
    if (strcmp(up, "PAUSE") == 0) {
        struct val v;
        cv.i++;
        v = expr();
        /* mm_wait, not mm_pause, for a program with an interrupt or a
           PULSE to service: it is the same wait cut into slices with the
           poll between them, which is what makes a SETTICK handler fire
           during a PAUSE the way MMBasic's does.  A program with nothing
           armed emits the plain one and pays nothing.  Decided in the
           scan pass, so a PAUSE textually ahead of the SETTICK still
           gets the serviced form. */
        if (cv.uses_interrupts || cv.uses_pulse) {
            cv.uses_wait = 1;
            emit(sfmt("mm_wait(%s);", as_flt(v)));
        } else {
            emit(sfmt("mm_pause(%s);", as_flt(v)));
        }
        return;
    }
    if (strcmp(up, "PULSE") == 0) {
        /* PULSE pin, width_ms - invert the pin for that long.  Under
           3 ms it blocks and is exact; longer and it returns at once and
           the pin flips back later.  See mmb_pulse.h. */
        const char *pin, *width;

        cv.i++;
        pin = as_int(expr());
        expect_op(",");
        width = as_flt(expr());
        cv.uses_gpio = 1;
        cv.uses_pulse = 1;
        emit(sfmt("mmg_pulse(%s, %s);", pin, width));
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
        cv.i += 2;
        do_on_error();
        return;
    }
    if (strcmp(up, "ON") == 0 && is_kw("KEY", 1)) {
        cv.i += 2;
        do_on_key();
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
        do_array_cmd(0);
        return;
    }
    if (strcmp(up, "MATH") == 0 && !is_op("(", 1)) {
        cv.i++;
        do_array_cmd(1);
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

static const char *load_prog(const char *up)
{
    if (strcmp(up, "LOAD") != 0)
        return NULL;
    if (is_kw("IMAGE", 1) || is_kw("BMP", 1))
        return "loadimage";
    if (is_kw("JPG", 1))
        return "loadjpg";
    if (is_kw("PNG", 1))
        return "loadpng";
    return NULL;
}

static void do_print(void)
{
    const char *chan = NULL;
    int suppress_nl = 0;
    int last = -1;               /* where the last item was emitted */

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
            last = last_line();
            suppress_nl = 1;
            continue;
        }
        if (is_op("@", 0)) {
            /* PRINT @(x, y [, mode]) - MMBasic's fun_at.  It is a
               FUNCTION returning "", not a statement, so it sits in the
               item list like anything else and needs no line of its
               own; MMBasic parses it the same way, which is why nothing
               separates it from the text that follows. */
            const char *x, *y, *mode = "0";
            cv.i++;
            expect_op("(");
            x = as_int(expr());
            expect_op(",");
            y = as_int(expr());
            if (accept_op(","))
                mode = as_int(expr());
            expect_op(")");
            if (chan != NULL)
                cv_err("PRINT @ positions text on the screen, so it "
                       "cannot be used with a file channel");
            /* mm_at returns a string temporary, so the statement has to
               release the previous one.  Without this a PRINT @ inside
               a loop runs out of temporaries after MM_TMPN turns and
               dies with "String expression too complex" - which is
               exactly the shape a counter redrawn every frame has. */
            cv.tmp_used = 1;
            emit(prcall(chan, "s", sfmt("mm_at(%s, %s, %s)", x, y, mode)));
            last = last_line();
            suppress_nl = 0;
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
        last = last_line();
    }
    if (!suppress_nl)
        emit(prcall(chan, "nl", NULL));
    else if (chan == NULL && last >= 0) {
        /* PRINT "x"; - no newline, but the text still belongs on screen
           now.  stdio is line buffered on a terminal, so without a
           flush it waits for the NEXT newline: a program that prints
           "Calculating... " and then works for half a minute shows
           nothing until it has finished.  A file channel needs no such
           thing and would only be slowed.

           The flush rides on the LAST item's call - mm_pr_s becomes
           mm_pr_se - rather than being a statement after it.  One extra
           statement in main cost the KnivD benchmark 32,400 grains
           against 12,150, because on the board's compiler it tips the
           function out of native code; the host build does not do it,
           so no gate would have caught it. */
        const char *ln = cv.out->lines[last];
        const char *p = strchr(ln, '(');
        if (p != NULL) {
            char *nu = palloc(strlen(ln) + 2);
            size_t pre = (size_t)(p - ln);
            memcpy(nu, ln, pre);
            nu[pre] = 'e';
            strcpy(nu + pre + 1, p);
            cv.out->lines[last] = nu;
        }
    }
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

/* mmb2c.py's int_target.  An interrupt target that may be a literal 0
   meaning "off". */
static const char *int_target(void)
{
    struct tok *t = peek(0);

    if (t && t->kind == T_NUM && strcmp(t->text, "0") == 0) {
        nxt();
        return "0";
    }
    return int_handler();
}

/* mmb2c.py's do_on_key.

     ON KEY handler          fires while a key is waiting
     ON KEY 0                off
     ON KEY code, handler    fires on that key, which is consumed
     ON KEY code, 0          off

   The two forms differ in what happens to the key, and that is the
   point of having both: the any-key form leaves it for INKEY$ in the
   handler, the specific form eats it (PicoMite.c:932-935).  MMBasic
   tells them apart by the argument count; here the first item does it -
   a name is a handler, a number is a key code. */
static void do_on_key(void)
{
    struct tok *t;
    const char *code, *fn;

    cv.uses_interrupts = 1;
    t = peek(0);
    if (t && t->kind == T_ID) {
        fn = int_handler();
        if (!fn)
            return;
        emit(sfmt("mmi_onkey_any(%s);", fn));
        return;
    }
    code = as_int(expr());
    if (!accept_op(",")) {
        /* "ON KEY 0" with nothing after it is the any-key form off. */
        emit("mmi_onkey_any(0);");
        return;
    }
    fn = int_target();
    if (!fn)
        return;
    emit(sfmt("mmi_onkey_sel(%s, %s);", code, fn));
}

/* mmb2c.py's do_spi.

     SPI OPEN speed, mode [, bits]
     SPI WRITE n, d1 [, d2 ...] | n, array() | n, string$
     SPI READ  n, var
     SPI CLOSE

   The FIRST controller: SPI2 is the second one, which on this board is
   the SD card's, so it is not offered.  Chip select is the program's,
   as it is on a PicoMite. */

static void do_spi(void)
{
    const char *n;
    int wr;

    cv.i++;
    cv.uses_spi = 1;
    if (accept_kw("CLOSE")) {
        emit("mmspi_close();");
        return;
    }
    if (accept_kw("OPEN")) {
        const char *speed = as_int(expr());
        const char *mode;
        const char *bits = "8";

        expect_op(",");
        mode = as_int(expr());
        if (accept_op(","))
            bits = as_int(expr());
        emit(sfmt("mmspi_open(__mmspi_a, __mmspi_b, __mmspi_c, "
                  "%s, %s, %s);", speed, mode, bits));
        return;
    }
    wr = accept_kw("WRITE");
    if (!wr && !accept_kw("READ")) {
        cv_err("SPI takes OPEN, WRITE, READ or CLOSE");
        return;
    }
    n = as_int(expr());
    expect_op(",");
    cv.tmp_used = 1;
    if (wr)
        comms_tx("SPI WRITE", n,
                 sfmt("  mmspi_write(%s, %%s);", n),
                 sfmt("  mmspi_write_bytes(%s, %%s);", n));
    else
        comms_rx("SPI READ", n, sfmt("  mmspi_read(%s, %%s);", n),
                 sfmt("  mmspi_read_bytes(%s, %%s);", n));
}

/* mmb2c.py's do_web.  The WEB family, arriving in stages
   (PLAN-web.md §11).  Stage 1, UDP:

     WEB UDP SERVER PORT n     bind the receive socket
     WEB UDP INTERRUPT sub|0   fire on a received datagram
     WEB UDP SEND ip$, port, msg$

   Stage 2, the TCP client:

     WEB OPEN TCP CLIENT host$, port [,timeout]
     WEB TCP CLIENT REQUEST req$, a%() [,timeout]
     WEB TCP CLIENT READ a%() [,timeout]
     WEB TCP CLIENT WRITE ls%() [,timeout]
     WEB CLOSE TCP CLIENT

   SERVER PORT is the WebMite's saved OPTION UDP SERVER PORT as a
   statement - a compiled program owns its own sockets (PLAN-web.md
   §3.2); the OPTION spelling is accepted as an alias so WebMite
   listings move across unedited.  Anything else under WEB names the
   stage it is waiting on rather than pretending to be an unknown
   command. */
static void do_web(void)
{
    if (accept_kw("UDP")) {
        if (is_kw("SERVER", 0) && is_kw("PORT", 1)) {
            cv.i += 2;
            cv.uses_udp = 1;
            emit(sfmt("mmg_udp_port(%s);", as_int(expr())));
            return;
        }
        if (accept_kw("INTERRUPT")) {
            cv.uses_udp = 1;
            cv.uses_interrupts = 1;
            emit(sfmt("mmi_udp_int(%s);", int_target()));
            return;
        }
        if (accept_kw("SEND")) {
            const char *ip, *port, *msg;

            cv.uses_udp = 1;
            ip = as_str(expr());
            expect_op(",");
            port = as_int(expr());
            expect_op(",");
            msg = as_str(expr());
            emit(sfmt("mmg_udp_send(%s, %s, %s);", ip, port, msg));
            return;
        }
        cv_err("WEB UDP takes SERVER PORT, INTERRUPT or SEND");
    }
    if (accept_kw("OPEN")) {
        /* TCP and TLS CLIENT differ by one flag: the protocol on the
           socket, and the SNI name the header sends first - the two
           lines tlsget.c promised. */
        int tls = -1;

        if (is_kw("TCP", 0) && is_kw("CLIENT", 1))
            tls = 0;
        else if (is_kw("TLS", 0) && is_kw("CLIENT", 1))
            tls = 1;
        if (tls >= 0) {
            const char *host, *port, *tmo = "5000";

            cv.i += 2;
            cv.uses_webclient = 1;
            host = as_str(expr());
            expect_op(",");
            port = as_int(expr());
            if (accept_op(","))
                tmo = as_int(expr());
            emit(sfmt("mmg_webc_open(%s, %s, %s, %d);",
                      host, port, tmo, tls));
            return;
        }
        cv_err("this WEB OPEN form is not implemented yet - the "
               "family arrives in stages (PLAN-web.md)");
    }
    if (is_kw("TLS", 0)) {
        if (is_kw("CA", 1)) {
            cv.i += 2;
            cv.uses_webclient = 1;
            emit(sfmt("mmg_webc_tlsca(%s);", as_str(expr())));
            return;
        }
        if (is_kw("NOVERIFY", 1)) {
            cv.i += 2;
            cv.uses_webclient = 1;
            emit("mmg_webc_tlsnoverify();");
            return;
        }
        cv_err("WEB TLS takes CA or NOVERIFY");
    }
    if (accept_kw("CLOSE")) {
        if (is_kw("TCP", 0) && is_kw("CLIENT", 1)) {
            cv.i += 2;
            cv.uses_webclient = 1;
            emit("mmg_webc_close();");
            return;
        }
        cv_err("this WEB CLOSE form is not implemented yet - the "
               "family arrives in stages (PLAN-web.md)");
    }
    if (is_kw("TCP", 0) && is_kw("CLIENT", 1)) {
        struct flat ls;
        const char *tmo;

        cv.i += 2;
        cv.uses_webclient = 1;
        if (accept_kw("REQUEST")) {
            const char *req = as_str(expr());

            expect_op(",");
            ls = lsref();
            tmo = "5000";
            if (accept_op(","))
                tmo = as_int(expr());
            emit(sfmt("mmg_webc_request(%s, %s, %s, %s);",
                      req, ls.ptr, ls.cnt, tmo));
            return;
        }
        if (accept_kw("READ")) {
            ls = lsref();
            tmo = "5000";
            if (accept_op(","))
                tmo = as_int(expr());
            emit(sfmt("mmg_webc_read(%s, %s, %s);",
                      ls.ptr, ls.cnt, tmo));
            return;
        }
        if (accept_kw("WRITE")) {
            ls = lsref();
            tmo = "10000";
            if (accept_op(","))
                tmo = as_int(expr());
            emit(sfmt("mmg_webc_write(%s, %s);", ls.ptr, tmo));
            return;
        }
        cv_err("WEB TCP CLIENT takes REQUEST, READ or WRITE "
               "(STREAM arrives in stages - PLAN-web.md)");
    }
    /* the server family - stage 4 */
    if (is_kw("TCP", 0) && is_kw("SERVER", 1) && is_kw("PORT", 2)) {
        cv.i += 3;
        cv.uses_webserver = 1;
        emit(sfmt("mmg_webs_port(%s);", as_int(expr())));
        return;
    }
    if (is_kw("TCP", 0) && is_kw("INTERRUPT", 1)) {
        cv.i += 2;
        cv.uses_webserver = 1;
        cv.uses_interrupts = 1;
        emit(sfmt("mmi_webs_int(%s);", int_target()));
        return;
    }
    if (is_kw("TCP", 0) && is_kw("READ", 1)) {
        struct flat ls;
        const char *conn;

        cv.i += 2;
        cv.uses_webserver = 1;
        conn = as_int(expr());
        expect_op(",");
        ls = lsref();
        emit(sfmt("mmg_webs_read(%s, %s, %s);", conn, ls.ptr, ls.cnt));
        return;
    }
    if (is_kw("TCP", 0) && is_kw("SEND", 1)) {
        struct flat ls;
        const char *conn;

        cv.i += 2;
        cv.uses_webserver = 1;
        conn = as_int(expr());
        expect_op(",");
        ls = lsref();
        emit(sfmt("mmg_webs_send(%s, %s);", conn, ls.ptr));
        return;
    }
    if (is_kw("TCP", 0) && is_kw("CLOSE", 1)) {
        cv.i += 2;
        cv.uses_webserver = 1;
        emit(sfmt("mmg_webs_close(%s);", as_int(expr())));
        return;
    }
    if (accept_kw("TRANSMIT")) {
        if (accept_kw("CODE")) {
            const char *conn = as_int(expr());

            expect_op(",");
            cv.uses_webserver = 1;
            emit(sfmt("mmg_webs_code(%s, %s);", conn, as_int(expr())));
            return;
        }
        if (accept_kw("FILE")) {
            const char *conn, *fname, *mime;

            conn = as_int(expr());
            expect_op(",");
            fname = as_str(expr());
            expect_op(",");
            mime = as_str(expr());
            cv.uses_webserver = 1;
            emit(sfmt("mmg_webs_file(%s, %s, %s);", conn, fname, mime));
            return;
        }
        if (accept_kw("PAGE")) {
            do_web_page();
            return;
        }
        cv_err("WEB TRANSMIT takes CODE, FILE or PAGE");
    }
    if (accept_kw("NTP")) {
        /* WEB NTP [offset [, server$ [, timeout]]] - cmd_ntp
         * (MMntp.c) mapped onto ntpdate(8); mmb_net.h says how.  The
         * timeout is parsed and dropped: ntpdate carries its own
         * retry cadence, inside MMBasic's 5 s default. */
        const char *off = "0.0";
        const char *server = c_string_literal("");

        cv.uses_net = 1;
        if (!stmt_end()) {
            off = as_flt(expr());
            if (accept_op(",")) {
                server = as_str(expr());
                if (accept_op(","))
                    as_int(expr());
            }
        }
        emit(sfmt("mmg_web_ntp(%s, %s);", off, server));
        return;
    }
    if (accept_kw("PING")) {
        /* WEB PING addr$ [, count] - ping(8) with its output on the
         * console; the replicated WebMite build has no PING of its
         * own, so the mapping is the reference (PLAN-web.md 12.2). */
        const char *addr;
        const char *cnt = "4";

        cv.uses_net = 1;
        addr = as_str(expr());
        if (accept_op(","))
            cnt = as_int(expr());
        emit(sfmt("mmg_web_ping(%s, %s);", addr, cnt));
        return;
    }
    if (accept_kw("CONNECT")) {
        /* No arguments: the WebMite's link gate - error "WIFI not
         * connected" when the radio has no address.  With ssid$,
         * pass$: wifi(8) joins and waits, NOT persisted -
         * /etc/wifi.conf stays the owner of the boot-time join
         * (PLAN-web.md 12.2). */
        const char *ssid;
        const char *key;

        cv.uses_net = 1;
        if (stmt_end()) {
            emit("mmg_web_connect_chk();");
            return;
        }
        ssid = as_str(expr());
        expect_op(",");
        key = as_str(expr());
        emit("mm_run_begin();");
        emit(sfmt("mm_run_arg(%s);", c_string_literal("wifi")));
        emit(sfmt("mm_run_arg(%s);", ssid));
        emit(sfmt("mm_run_arg(%s);", key));
        emit("mm_run_exec();");
        return;
    }
    cv_err("this WEB command is not implemented yet - the family "
           "arrives in stages (PLAN-web.md)");
}

/* mmb2c.py's websub_norm: a page expression's table key - verbatim
   inside a "string literal", upcased with whitespace dropped outside
   one.  mm_webpg_next applies the same rule at run time, and the three
   MUST agree or a page's expressions stop matching. */
static int websub_norm(const char *s, int n, char *out, int max)
{
    int q = 0, o = 0, i;
    char c;

    for (i = 0; i < n && o < max - 1; i++) {
        c = s[i];
        if (c == '"') {
            q = !q;
            out[o++] = c;
            continue;
        }
        if (q) {
            out[o++] = c;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        out[o++] = c;
    }
    out[o] = 0;
    return o;
}

/* The fragment sub-parse's saved line: static, not stack - a board
   process cannot spare MAXTOKS of tok on its stack, and do_web_page
   never nests. */
static struct tok websub_save[MAXTOKS];

/* mmb2c.py's do_web_page.  WEB TRANSMIT PAGE conn, "file"
   [, bufsize] - PLAN-web.md §4, the call-site substitution: the page
   is read HERE, at translate time, and every {expression} in it is
   compiled through the normal expression pipeline INLINE in this
   statement, where the enclosing sub's locals and parameters are in
   scope.  The emitted switch is dispatched by mm_webpg_next matching
   each brace's normalised text against the __mmwebsub_N table. */
static void do_web_page(void)
{
    const char *conn, *bufsize = "4096", *fname;
    struct tok *t;
    char path[256], key[160];
    char *text;
    long size, got;
    FILE *f;
    struct websubtab *tab;
    int tno, i, n, j, kl, k, ci, saven, savei;
    const char *base;

    conn = as_int(expr());
    expect_op(",");
    t = peek(0);
    if (t == NULL || t->kind != T_STR)
        cv_err("WEB TRANSMIT PAGE needs a literal page name: a "
               "computed one cannot be pre-scanned (PLAN-web.md)");
    cv.i++;
    fname = pstr(t->text);
    if (accept_op(","))
        bufsize = as_int(expr());
    cv.uses_webserver = 1;
    if (cv.mode != M_EMIT)
        return;
    if (cv.nwebsubs >= MAXWEBSUB)
        cv_err("too many WEB TRANSMIT PAGE call sites");
    /* next to the program, or absolute - and it must exist NOW */
    base = strrchr(cv.srcname, '/');
    if (fname[0] == '/' || base == NULL)
        snprintf(path, sizeof(path), "%s", fname);
    else
        snprintf(path, sizeof(path), "%.*s/%s",
                 (int)(base - cv.srcname), cv.srcname, fname);
    f = fopen(path, "rb");
    if (f == NULL)
        cv_err(sfmt("cannot read page '%s': it must exist at "
                    "translate time, next to the program", fname));
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = malloc((size_t)size + 1);
    if (text == NULL) {
        fclose(f);
        cv_err("out of memory reading the page");
    }
    got = (long)fread(text, 1, (size_t)size, f);
    fclose(f);
    text[got < 0 ? 0 : got] = 0;
    n = (int)got;

    tab = &cv.websubs[cv.nwebsubs];
    tno = cv.nwebsubs++;
    tab->n = 0;

    /* pass 1 - mmb2c.py's websub_scan: every {expression}, raw text
       kept for compilation, deduped by normalised key */
    {
        static const char *raws[MAXWEBKEYS];

        i = 0;
        while (i < n) {
            char c = text[i++];
            char raw[160];
            int rn = 0;

            if (c != '{')
                continue;
            if (i < n && text[i] == '{') {
                i++;
                continue;
            }
            j = i;
            while (j < n && text[j] != '}')
                j++;
            while (i < j && rn < (int)sizeof(raw) - 1) {
                if (text[i] != '\x1a')
                    raw[rn++] = text[i];
                i++;
            }
            raw[rn] = 0;
            i = j + 1;
            kl = websub_norm(raw, rn, key, (int)sizeof(key));
            if (kl == 0)
                continue;
            for (k = 0; k < tab->n; k++)
                if (strcmp(tab->keys[k], key) == 0)
                    break;
            if (k < tab->n)
                continue;
            if (tab->n >= MAXWEBKEYS)
                cv_err("too many expressions in one page");
            raws[tab->n] = pstr(raw);
            tab->keys[tab->n] = pstr(key);
            tab->n++;
        }
        free(text);

        emit("{ struct mm_webpg __pg; int __pi;");
        emit(sfmt("mm_webpg_start(&__pg, %s, %s, %s);",
                  conn, c_string_literal(fname), bufsize));
        emit(sfmt("while ((__pi = mm_webpg_next(&__pg, __mmwebsub_%d, "
                  "%d)) >= 0) {", tno, tab->n));
        emit("    unsigned __pgm = mm_mark();");
        emit("    switch (__pi) {");

        /* pass 2: compile each raw text through the normal expression
           pipeline, IN the enclosing scope, via the non-resetting
           lexer (the suspended line's token texts stay live) */
        saven = cv.ntoks;
        savei = cv.i;
        memcpy(websub_save, cv.toks,
               (size_t)cv.ntoks * sizeof(struct tok));
        for (ci = 0; ci < tab->n; ci++) {
            struct val v;
            const char *put;

            cv.ntoks = tokenize_frag(raws[ci], cv.lineno, cv.toks);
            cv.i = 0;
            v = expr();
            if (!at_end())
                cv_err(sfmt("page expression '{%s}' does not parse as "
                            "one expression", raws[ci]));
            if (v.ty == TY_S)
                put = "mm_webpg_put_s";
            else if (v.ty == TY_F)
                put = "mm_webpg_put_f";
            else
                put = "mm_webpg_put_i";
            emit(sfmt("    case %d: %s(&__pg, %s); break;", ci, put,
                      v.code));
        }
        cv.ntoks = saven;
        cv.i = savei;
        memcpy(cv.toks, websub_save,
               (size_t)saven * sizeof(struct tok));
    }

    emit("    }");
    emit("    mm_release(__pgm);");
    emit("}");
    emit("mm_webpg_send(&__pg); }");
}

/* mmb2c.py's do_onewire.

     ONEWIRE RESET pin
     ONEWIRE WRITE pin, flag, count, <data>
     ONEWIRE READ  pin, flag, count, <destination>

   The data and destination are the shared forms - MMBasic's owWrite
   and owRead call GetCommsTxData and GetCommsRxDest at argument 6,
   exactly as I2C does.  That is why one-wire waited for mmb_comms.h
   rather than growing a third copy of them. */
static void do_onewire(void)
{
    const char *pin, *flag, *n;
    int wr;

    cv.uses_gpio = 1;
    cv.uses_onewire = 1;
    if (accept_kw("RESET")) {
        emit(sfmt("(void)mmow_reset(%s);", as_int(expr())));
        return;
    }
    wr = accept_kw("WRITE");
    if (!wr && !accept_kw("READ")) {
        cv_err("ONEWIRE takes RESET, WRITE or READ");
        return;
    }
    pin = as_int(expr());
    expect_op(",");
    flag = as_int(expr());
    expect_op(",");
    n = as_int(expr());
    expect_op(",");
    cv.tmp_used = 1;
    if (wr)
        comms_tx("ONEWIRE WRITE", n,
                 sfmt("  mmow_write(%s, %s, %s, %%s);", pin, flag, n),
                 sfmt("  mmow_write_bytes(%s, %s, %s, %%s);",
                      pin, flag, n));
    else
        comms_rx("ONEWIRE READ", n,
                 sfmt("  mmow_read(%s, %s, %s, %%s);", pin, flag, n),
                 sfmt("  mmow_read_bytes(%s, %s, %s, %%s);",
                      pin, flag, n));
}

/* mmb2c.py's comms_tx / comms_rx: the data arguments I2C, SPI and
   one-wire share.  MMBasic has ONE implementation of these and three
   callers (GetCommsTxData / GetCommsRxDest / PutCommsRxData in
   MMBasic.c, reached from I2C.c, Onewire.c and SPI.c), so this has one
   too - the two copies it replaced had drifted five ways.  See
   mmb_comms.h.

   callfmt carries the transfer statement with a single %s where the
   buffer or byte pointer goes; the Python passes a closure for the same
   job. */
static void comms_tx(const char *what, const char *n,
                     const char *callfmt, const char *bytesfmt)
{
    const char *v[64];
    int nv = 0, i;
    struct val v0;

    cv.uses_comms = 1;
    if (accept_kw("LONGSTRING")) {
        /* SPI WRITE n, LONGSTRING a() - the bytes of a long string, with
           no 255-byte cap and no copy.  Spelled out because a long
           string IS an integer array: written a() it is a numeric array
           and sends one byte per eight-byte cell, which is MMBasic's
           behaviour and stays. */
        struct flat f = lsref();

        emit(sfmt("{ const unsigned char *__b = mmc_tx_ls(%s, %s);",
                  f.ptr, n));
        emit(sfmt(bytesfmt, "__b"));
        emit("}");
        return;
    }
    if (is_array_arg()) {
        struct sym *sy = arrayref(1);
        struct flat f;

        if (sy->ty == TY_S) {
            cv_err(sfmt("%s needs a numeric array, and '%s' is a string "
                        "array", what, sy->name));
            return;
        }
        f = array_flat(sy);
        emit(sfmt("{ unsigned int *__b = mmc_buf_for(%s);", n));
        emit(sfmt("  mmc_tx_arr_%s(__b, %s, %s, %s);",
                  sy->ty == TY_I ? "i" : "f", n, f.ptr, f.cnt));
        emit(sfmt(callfmt, "__b"));
        emit("}");
        return;
    }
    v0 = expr();
    if (v0.ty == TY_S && !is_op(",", 0)) {
        /* A string: no copy, and no buffer.  MMBasic copies because its
           buffer is the only path it has; mmc_tx_str only checks the
           length and hands back where the bytes already are. */
        emit(sfmt("{ const unsigned char *__b = mmc_tx_str(%s, %s);",
                  v0.code, n));
        emit(sfmt(bytesfmt, "__b"));
        emit("}");
        return;
    }
    /* A list of expressions.  MMBasic requires as many as the count says
       and raises "Argument count" otherwise - which the old per-bus code
       did not check, so a short list left the driver reading past the
       buffer. */
    v[nv++] = as_int(v0);
    while (accept_op(",") && nv < 64)
        v[nv++] = as_int(expr());
    emit("{ unsigned int *__b;");
    emit(sfmt("  mmc_count(%s, %d);", n, nv));
    emit(sfmt("  __b = mmc_buf_for(%s);", n));
    for (i = 0; i < nv; i++)
        emit(sfmt("  __b[%d] = (unsigned int)(%s);", i, v[i]));
    emit(sfmt(callfmt, "__b"));
    emit("}");
}

static void comms_rx(const char *what, const char *n, const char *callfmt,
                     const char *bytesfmt)
{
    const char *tg[64];
    int nt = 0, i;
    struct tok *t;

    cv.uses_comms = 1;
    if (accept_kw("LONGSTRING")) {
        /* ... and the same as a destination, which is how a program
           reads more than 255 bytes back. */
        struct flat f = lsref();

        emit(sfmt("{ unsigned char *__b = mmc_rx_ls(%s, %s, %s);",
                  f.ptr, f.cnt, n));
        emit(sfmt(bytesfmt, "__b"));
        emit("}");
        return;
    }
    if (is_array_arg()) {
        struct sym *sy = arrayref(1);
        struct flat f;

        if (sy->ty == TY_S) {
            cv_err(sfmt("%s needs a numeric array, and '%s' is a string "
                        "array", what, sy->name));
            return;
        }
        f = array_flat(sy);
        /* Checked BEFORE the transfer, as GetCommsRxDest is: a read
           moves the bus, so a destination that cannot hold the answer
           has to be refused before it does. */
        emit(sfmt("{ unsigned int *__b = mmc_buf_for(%s);", n));
        emit(sfmt("  mmc_rx_fits(%s, %s);", f.cnt, n));
        emit(sfmt(callfmt, "__b"));
        emit(sfmt("  mmc_rx_arr_%s(%s, %s, __b, %s);",
                  sy->ty == TY_I ? "i" : "f", f.ptr, f.cnt, n));
        emit("}");
        return;
    }
    t = peek(0);
    if (t != NULL && t->kind == T_ID && !is_op(",", 1)) {
        struct sym *s = reference(t->text, is_op("(", 1));

        if (s->ty == TY_S && !s->is_array) {
            cv.i++;
            emit(sfmt("{ unsigned int *__b = mmc_buf_for(%s);", n));
            emit(sfmt("  mmc_rx_strfits(%s);", n));
            emit(sfmt(callfmt, "__b"));
            emit(sfmt("  mmc_rx_str(%s, __b, %s);", s->acc, n));
            emit("}");
            return;
        }
    }
    /* A list of lvalues, one per value received - MMBasic's
       COMMS_RXD_LIST, which was missing here entirely.  A single scalar
       is the same form with one element, which is also MMBasic's rule
       that the count must then be 1. */
    tg[nt++] = lvalue_from_here();
    while (accept_op(",") && nt < 64)
        tg[nt++] = lvalue_from_here();
    emit("{ unsigned int *__b;");
    emit(sfmt("  mmc_count(%s, %d);", n, nt));
    emit(sfmt("  __b = mmc_buf_for(%s);", n));
    emit(sfmt(callfmt, "__b"));
    for (i = 0; i < nt; i++)
        emit(sfmt("  %s = __b[%d];", tg[i], i));
    emit("}");
}

/* mmb2c.py's do_i2c0.

     I2C WRITE addr, option, count, d1 [, d2 ...]
     I2C READ  addr, option, count, <destination>
     I2C CHECK addr

   The FIXED bus: GP20/GP21, the QWIIC socket and the DS3231 together.
   No SETPIN, no OPEN, no CLOSE - the pins are the board's and the
   controller is already running for the clock, which is why MMBasic's
   cmd_i2c has no pin test where cmd_i2c2 errors "Pin not set for I2C2".

   NOTHING HERE RAISES: MMBasic records the outcome in MM.I2C and
   returns.  See mmb_i2c.h. */
static void do_i2c0(void)
{
    const char *addr, *opt, *n;
    int wr;

    cv.i++;
    cv.uses_i2c0 = 1;
    if (accept_kw("CHECK")) {
        emit(sfmt("mmi2c0_check(%s);", as_int(expr())));
        return;
    }
    if (is_kw("OPEN", 0) || is_kw("CLOSE", 0)) {
        cv_err("the fixed I2C bus is always open - GP20/GP21 are the "
               "board's and the controller runs for the clock; OPEN and "
               "CLOSE are I2C2's");
        return;
    }
    wr = accept_kw("WRITE");
    if (!wr && !accept_kw("READ")) {
        cv_err("I2C takes WRITE, READ or CHECK");
        return;
    }
    addr = as_int(expr());
    expect_op(",");
    opt = as_int(expr());
    expect_op(",");
    n = as_int(expr());
    expect_op(",");
    cv.tmp_used = 1;
    if (wr)
        comms_tx("I2C WRITE", n,
                 sfmt("  mmi2c0_write(%s, %s, %s, %%s);", addr, opt, n),
                 sfmt("  mmi2c0_write_bytes(%s, %s, %s, %%s);",
                      addr, opt, n));
    else
        comms_rx("I2C READ", n,
                 sfmt("  mmi2c0_read(%s, %s, %s, %%s);", addr, opt, n),
                 sfmt("  mmi2c0_read_bytes(%s, %s, %s, %%s);",
                      addr, opt, n));
}

/* mmb2c.py's do_i2c2.

     I2C2 OPEN speed, timeout
     I2C2 WRITE addr, option, count, d1 [, d2 ...]
     I2C2 READ  addr, option, count, array()
     I2C2 CLOSE

   The second controller, on whatever header pins SETPIN gave it.
   MMBasic's split: the fixed bus needs no OPEN because it has fixed
   pins, and this one does because it has none. */
static void do_i2c2(void)
{
    const char *addr, *opt, *n;
    int wr;

    cv.i++;
    cv.uses_i2c = 1;
    if (accept_kw("CLOSE")) {
        emit("mmi2c_close();");
        return;
    }
    if (accept_kw("OPEN")) {
        const char *speed = as_int(expr());
        const char *tmo;
        expect_op(",");
        tmo = as_int(expr());
        emit(sfmt("mmi2c_open(__mmi2c_sda, __mmi2c_scl, %s, %s);",
                  speed, tmo));
        return;
    }
    wr = accept_kw("WRITE");
    if (!wr && !accept_kw("READ")) {
        cv_err("I2C2 takes OPEN, WRITE, READ or CLOSE");
        return;
    }
    addr = as_int(expr());
    expect_op(",");
    opt = as_int(expr());
    expect_op(",");
    n = as_int(expr());
    expect_op(",");
    cv.tmp_used = 1;
    /* All three of MMBasic's forms for the data, because its own BMP180
       example uses two of them in the same program: a list of byte
       expressions, a whole numeric array written a(), and a STRING -
       and the string is the interesting one, since STR2BIN() then pulls
       the sensor's 16- and 32-bit fields straight out of what was read.
       A read that only knew about arrays could not run that program at
       all. */
    if (wr)
        comms_tx("I2C2 WRITE", n,
                 sfmt("  mmi2c_write(%s, %s, %s, %%s);", addr, opt, n),
                 sfmt("  mmi2c_write_bytes(%s, %s, %s, %%s);",
                      addr, opt, n));
    else
        comms_rx("I2C2 READ", n,
                 sfmt("  mmi2c_read(%s, %s, %s, %%s);", addr, opt, n),
                 sfmt("  mmi2c_read_bytes(%s, %s, %s, %%s);",
                      addr, opt, n));
}

/* mmb2c.py's settick_id.  SETTICK's optional trailing timer number,
   1-4.  Absent is 1, which is MMBasic's irq = 0 when the argument is
   missing. */
static const char *settick_id(void)
{
    if (accept_op(","))
        return as_int(expr());
    return "1";
}

/* mmb2c.py's do_settick.

     SETTICK period, handler [, n]
     SETTICK 0, 0 [, n]          off
     SETTICK PAUSE|RESUME [, n]

   MMBasic counts milliseconds in an interrupt and fires when the count
   passes the period; this keeps a microsecond deadline and asks at the
   poll that is already happening.  The observable rules are copied:
   four timers, missed periods dropped rather than queued, and PAUSE
   freezing the time-to-go where it stands. */
static void do_settick(void)
{
    const char *ms, *fn;

    cv.i++;
    cv.uses_interrupts = 1;
    if (accept_kw("PAUSE")) {
        emit(sfmt("mmi_settick_pause(%s, 0);", settick_id()));
        return;
    }
    if (accept_kw("RESUME")) {
        emit(sfmt("mmi_settick_pause(%s, 1);", settick_id()));
        return;
    }
    ms = as_int(expr());
    expect_op(",");
    /* "SETTICK 0, 0" turns a timer off, and its handler slot is a
       literal 0 rather than a name - so the target is only resolved
       when there is one to resolve. */
    fn = int_target();
    if (!fn)
        return;
    emit(sfmt("mmi_settick(%s, %s, %s);", ms, fn, settick_id()));
}

/* mmb2c.py's setpin_pull.  MMBasic's optional PULLUP / PULLDOWN on an
   input SETPIN.  Absent means neither, which is MMBasic's default
   (External.c:1918-1935 leaves option = 0).  Hysteresis is not an
   option in either place - every digital input gets the Schmitt
   trigger. */
static const char *setpin_pull(void)
{
    if (!accept_op(","))
        return "0";
    if (accept_kw("PULLUP"))
        return "1";
    if (accept_kw("PULLDOWN"))
        return "-1";
    cv_err("SETPIN's last argument is PULLUP or PULLDOWN");
    return "0";
}

/* mmb2c.py's int_handler.  Resolve an interrupt target to the C
   function that is it.

   MMBasic's GetIntAddress (MM_Misc.c:10250) takes a SUB name, a label
   or a line number.  Only the SUB survives translation: compiled code
   cannot jump into the middle of a function from a poll site, so labels
   and line numbers are refused here with a clear message rather than
   half-working - the ON ERROR RESTART precedent.  The SUB is otherwise
   an ordinary generated function and may still be called normally.

   Returns NULL after cv_err, which does not return in the strict path
   but does in the lenient one. */
static const char *int_handler(void)
{
    struct tok *t = nxt();
    struct routine *r;
    char *canon;
    int ty;

    if (t->kind != T_ID) {
        cv_err("an interrupt handler must be the name of a SUB "
               "(MMBasic's labels and line numbers are not translated)");
        return NULL;
    }
    canon = split_suffix(t->up, &ty);
    r = routine_get(canon);
    if (r == NULL) {
        cv_err(sfmt("no SUB called '%s' to handle the interrupt",
                    t->text));
        return NULL;
    }
    if (r->is_func) {
        cv_err(sfmt("'%s' is a FUNCTION; an interrupt handler must be "
                    "a SUB", r->disp));
        return NULL;
    }
    if (r->nparams) {
        cv_err(sfmt("interrupt handler '%s' must take no parameters",
                    r->disp));
        return NULL;
    }
    return r->cname;
}

static void do_longstring(void)
{
    struct tok *t = nxt();
    const char *op;

    if (t->kind != T_ID)
        cv_err("LONGSTRING needs a sub-command");
    op = t->up;
    cv.uses_lstring = 1;

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
            const char *b = "-1LL";
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
    cv.uses_misc = 1;
    emit(sfmt("mm_gosub_push(%d); goto %s;", site, clabel(canon)));
    raw(sfmt("__GR%d: ;", site));
}

static void do_return(void)
{
    struct gsub *g = gsub_find(gosub_key());
    int k;

    if (g == NULL || g->n == 0)
        cv_err("RETURN without any GOSUB in this part of the program");
    cv.uses_misc = 1;
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
    cv.uses_data = 1;
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
            if (sym->ty == TY_S) {
                cv.reads_string = 1;
                emit(sfmt("    mm_sset((%s)[%s], mm_read_s()); }",
                          f.ptr, k));
            }
            else
                emit(sfmt("    (%s)[%s] = mm_read_%s(); }",
                          f.ptr, k, sym->ty == TY_I ? "i" : "f"));
            cv.tmp_used = 1;
        } else {
            int cap;
            struct val tgt = input_target(&cap);
            if (tgt.ty == TY_S) {
                cv.reads_string = 1;
                emit(swrite_cap(cap, tgt.code, "mm_read_s()"));
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
    cv.uses_data = 1;
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
    cv.uses_sort = 1;
    emit(sfmt("mm_sort_%s(%s, %s, %s, (int)(%s), (int)(%s), (int)(%s));",
              kind, f.ptr, idx, f.cnt, start, count, flags));
}

/* -- PIXEL, the array form -------------------------------------------- */

/* The smallest of some element counts, as a C expression.

   Textually identical counts collapse to one term, which is the usual
   case - the arrays are dimensioned together - so this normally emits
   no comparison at all. */
static const char *shortest(const char **counts, int n)
{
    const char *seen[3];
    int nseen = 0, i, j;
    const char *e;

    for (i = 0; i < n; i++) {
        for (j = 0; j < nseen; j++)
            if (strcmp(seen[j], counts[i]) == 0)
                break;
        if (j == nseen)
            seen[nseen++] = counts[i];
    }
    e = seen[0];
    for (i = 1; i < nseen; i++)
        e = sfmt("((%s) < (%s) ? (%s) : (%s))", e, seen[i], e, seen[i]);
    return e;
}

/* PIXEL xa(), ya() [, c | ca()] - MMBasic's array form.

   Draw.c cmd_pixel, the branch it takes when getargaddress reports more
   than one element.  One call for the whole run: a syscall costs 1.3us
   and a pixel store 15ns, so plotting point by point spends its time
   crossing into the kernel rather than drawing.

   One deviation from MMBasic, and only in a program that is already
   wrong: it takes the count from the Y array and clamps it to the
   colour array, so an X array shorter than Y is read past its end.
   This takes the shortest of the three.  For arrays dimensioned
   together - every correct program - they agree. */
static void do_pixels(void)
{
    struct sym *xsym, *ysym;
    struct flat xf, yf;
    const char *cfp = "NULL", *cip = "NULL", *rgb = "MM_CUR";
    const char *counts[3];
    int ncounts = 0;
    const char *xfp, *xip, *yfp, *yip;

    xsym = arrayref(1);
    xf = array_flat(xsym);
    expect_op(",");
    ysym = arrayref(1);
    yf = array_flat(ysym);
    if (xsym->ty == TY_S)
        cv_err("PIXEL needs numeric coordinate arrays, and '%s' is a "
               "string array", xsym->name);
    if (ysym->ty == TY_S)
        cv_err("PIXEL needs numeric coordinate arrays, and '%s' is a "
               "string array", ysym->name);
    counts[ncounts++] = xf.cnt;
    counts[ncounts++] = yf.cnt;
    if (accept_op(",") && !stmt_end()) {
        if (is_array_arg()) {
            struct sym *csym = arrayref(1);
            struct flat cf = array_flat(csym);
            if (csym->ty == TY_S)
                cv_err("the PIXEL colour array must be numeric, and '%s' "
                       "is a string array", csym->name);
            if (csym->ty == TY_F)
                cfp = cf.ptr;
            else
                cip = cf.ptr;
            counts[ncounts++] = cf.cnt;
        } else {
            /* a single colour for the whole run - MMBasic's nc == 1 */
            rgb = as_int(expr());
        }
    }
    if (xsym->ty == TY_F) { xfp = xf.ptr; xip = "NULL"; }
    else                  { xfp = "NULL"; xip = xf.ptr; }
    if (ysym->ty == TY_F) { yfp = yf.ptr; yip = "NULL"; }
    else                  { yfp = "NULL"; yip = yf.ptr; }
    emit(sfmt("mm_pixels(%s, %s, %s, %s, %s, %s, %s, %s);",
              xfp, xip, yfp, yip, cfp, cip, rgb,
              shortest(counts, ncounts)));
}

/* -- INC / CAT / ERASE ----------------------------------------------- */

static void do_inc(void)
{
    int cap;
    struct val tgt = input_target(&cap);
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
        emit(swrite_cap(cap, tgt.code,
                        sfmt("mm_scat(%s, %s)", tgt.code, v.code)));
        cv.tmp_used = 1;
    } else if (tgt.ty == TY_I) {
        emit(sfmt("%s += %s;", tgt.code, as_int(v)));
    } else {
        emit(sfmt("%s += %s;", tgt.code, as_flt(v)));
    }
}

static void do_cat(void)
{
    int cap;
    struct val tgt = input_target(&cap);
    struct val v;

    if (tgt.ty != TY_S)
        cv_err("CAT needs a string variable");
    expect_op(",");
    v = expr();
    if (v.ty != TY_S)
        cv_err("CAT needs a string to append");
    emit(swrite_cap(cap, tgt.code,
                    sfmt("mm_scat(%s, %s)", tgt.code, v.code)));
    cv.tmp_used = 1;
}

static void do_erase(void)
{
    int warned = 0;

    while (!stmt_end()) {
        struct tok *t = peek(0);
        struct sym *sym;
        if (t == NULL || t->kind != T_ID)
            cv_err("ERASE needs a variable name");
        sym = reference(t->text, 0);
        cv.i++;
        if (accept_op("("))
            expect_op(")");
        if (sym->is_array && sym->dynamic && !sym->is_param) {
            /* This one really is given back: its elements are on the
               heap, so ERASE frees them and leaves it undimensioned,
               exactly as the interpreter's erase() does. */
            const char *old = newtmp("ae");
            cv.tmp_used = 1;
            emit(sfmt("{ void *%s = %s;", old, sym->acc));
            emit(sfmt("  %s = 0; %s[0] = 0;", sym->acc, sym->bacc));
            emit(sfmt("  if (%s) mm_lfree(%s); }", old, old));
        } else {
            if (!warned) {
                cv_warn("ERASE zeroes the variable; static storage "
                        "cannot be handed back the way the "
                        "interpreter does");
                warned = 1;
            }
            emit(zero_of(sym));
        }
        if (!accept_op(","))
            break;
    }
}

char *zero_of(struct sym *sym)
{
    if (sym->stype != NULL) {
        if (sym->is_array)
            return sfmt("memset(%s, 0, sizeof %s);", sym->acc,
                        sym->acc);
        return sfmt("memset(&%s, 0, sizeof %s);", sym->acc, sym->acc);
    }
    if (sym->is_array) {
        struct flat f = array_flat(sym);
        cv.uses_array = 1;
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

/* -- ON ERROR --------------------------------------------------------- */

/* ABORT | CLEAR | IGNORE | SKIP [n]  (cmd_on, Commands.c:8299).  SKIP with
 * no count is 2 and SKIP n is n+1, because the ON ERROR statement
 * decrements the counter itself at its own end - so the count reaches the
 * next statement intact. */
static void do_on_error(void)
{
    struct tok *w = peek(0);
    const char *kw = (w != NULL && w->kind == T_ID) ? w->up : "";
    const char *n = "1";
    int mode;
    int nlit;

    if (strcmp(kw, "RESTART") == 0)
        cv_err("ON ERROR RESTART reboots the machine; a compiled "
               "program has no equivalent");
    if (strcmp(kw, "ABORT") == 0)
        mode = 0;
    else if (strcmp(kw, "CLEAR") == 0)
        mode = 1;
    else if (strcmp(kw, "IGNORE") == 0)
        mode = 2;
    else if (strcmp(kw, "SKIP") == 0)
        mode = 3;
    else {
        cv_err("ON ERROR ABORT|CLEAR|IGNORE|SKIP [n]");
        return;
    }
    cv.i += 1;
    cv.uses_onerror = 1;
    if (mode == 2)
        cv.onerror_global = 1;
    nlit = -1;
    if (mode == 3 && peek(0) != NULL && !is_op(":", 0)) {
        struct tok *w2 = peek(0);
        struct tok *w3 = peek(1);
        int alldig = (w2->kind == T_NUM && strcmp(w2->up, "I") == 0
                      && *w2->text);
        const char *p;

        if (alldig)
            for (p = w2->text; *p; p++)
                if (*p < '0' || *p > '9')
                    alldig = 0;
        if (alldig && (w3 == NULL ||
                       (w3->kind == T_OP && strcmp(w3->text, ":") == 0))) {
            nlit = atoi(w2->text);
            n = pstr(w2->text);
            cv.i += 1;
        } else {
            /* the count is a run-time value: the window cannot be laid
               out at compile time, so arm the whole program */
            n = as_int(expr());
            cv.onerror_global = 1;
        }
    } else if (mode == 3)
        nlit = 1;
    if (nlit >= 0)
        cv.err_window_pending = nlit;
    emit(sfmt("mm_on_error(%d, %s);", mode, n));
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

/* C_MUL has two spellings in MMBasic and C_MULT is the older one;
   cmd_math checks for both.  The value is the C operator. */
static const struct { const char *kw; char op; } ccomb[] = {
    { "C_ADD", '+' }, { "C_SUB", '-' }, { "C_MUL", '*' },
    { "C_MULT", '*' }, { "C_DIV", '/' }, { "C_AND", '&' },
    { "C_OR", '|' },  { "C_XOR", '^' }, { NULL, 0 }
};

static void do_array_cmd(int is_math)
{
    struct tok *t = nxt();
    const char *op;
    int k;

    if (t->kind != T_ID)
        cv_err("ARRAY/MATH needs a sub-command");
    op = t->up;
    for (k = 0; ccomb[k].kw; k++) {
        if (strcmp(op, ccomb[k].kw) != 0)
            continue;
        /* MATH C_ADD a(), b(), c()   - c(i) = a(i) op b(i)
         *
         * MATH only: MMBasic has these in cmd_math and nowhere else,
         * so ARRAY C_ADD is a syntax error there and is one here.
         *
         * The loops live in mmb_math.h, included only by a program that
         * asks for one - the same bargain as the graphics and pin
         * headers.  Not the runtime: fourteen one-line loops would be
         * fourteen more wrappers in bcrun and fourteen more names in
         * its table, carried by every program on the machine whether or
         * not it says C_ADD. */
        {
            struct sym *a, *b, *c;
            struct flat af, bf, cf;

            if (!is_math)
                cv_err("%s is a MATH sub-command, not an ARRAY one", op);
            a = arrayref(1);
            expect_op(",");
            b = arrayref(1);
            expect_op(",");
            c = arrayref(1);
            if (a->ty == TY_S || b->ty == TY_S || c->ty == TY_S)
                cv_err("%s does not apply to a string array", op);
            if (a->ty != b->ty || b->ty != c->ty)
                cv_err("%s needs all three arrays to be the same type",
                       op);
            af = array_flat(a);
            bf = array_flat(b);
            cf = array_flat(c);
            cv.uses_math = 1;
            emit(sfmt("mmg_carr_%s(%s, %s, %s, %s, %s, %s, '%c');",
                      a->ty == TY_I ? "i" : "f",
                      af.ptr, af.cnt, bf.ptr, bf.cnt, cf.ptr, cf.cnt,
                      ccomb[k].op));
        }
        return;
    }
    if (strcmp(op, "SET") == 0) {
        struct val val = expr();
        struct sym *sym;
        struct flat f;
        expect_op(",");
        sym = arrayref(1);
        f = array_flat(sym);
        cv.uses_array = 1;
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
        cv.uses_array = 1;
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
    if (strcmp(op, "SLICE") == 0 || strcmp(op, "INSERT") == 0) {
        /* ARRAY SLICE  from(), i1, , i3, to()     - read one line out
         * ARRAY INSERT into(), i1, , i3, from()   - write one line in
         *
         * MATH SLICE and MATH INSERT are the same two commands:
         * MMBasic's cmd_math calls array_slice and array_insert, the
         * very functions cmd_slice and cmd_insert call.
         *
         * Exactly one index is left blank, and that is the dimension
         * the line runs along.  The blank is a comma with nothing
         * before it, so it is recognised by finding a comma where an
         * expression should have started - and the array at the end is
         * recognised the way PIXEL recognises its array form, by the
         * a() spelling. */
        struct sym *arr, *line;
        const char *parts[MAXARGS];
        int nparts = 0, blank = -1;
        struct vec v;
        struct flat lf;
        const char *sfx;

        arr = arrayref(1);
        expect_op(",");
        while (!is_array_arg()) {
            if (nparts >= MAXARGS)
                cv_err("ARRAY %s: too many indices", op);
            if (is_op(",", 0)) {
                if (blank >= 0)
                    cv_err("ARRAY %s: only one index can be omitted", op);
                blank = nparts;
                parts[nparts++] = NULL;
            } else {
                parts[nparts++] = sfmt("(int)(%s)", as_int(expr()));
            }
            if (!accept_op(","))
                cv_err("ARRAY %s wants the one-dimensional array last, "
                       "written b()", op);
        }
        line = arrayref(1);
        if (blank < 0)
            cv_err("ARRAY %s: leave one index blank to say which "
                   "dimension the line runs along", op);
        if (arr->ty != line->ty)
            cv_err("ARRAY %s needs both arrays to be the same type "
                   "(MMBasic converts between integer and float here; "
                   "this does not, as ARRAY ADD does not)", op);
        v = array_vector(arr, parts, nparts, blank);
        lf = array_line(line);
        sfx = arr->ty == TY_I ? "i" : (arr->ty == TY_F ? "f" : "s");
        cv.uses_array = 1;
        if (strcmp(op, "SLICE") == 0)
            emit(sfmt("mm_arr_copy_%s(%s, 1, %s, %s, %s, %s);",
                      sfx, lf.ptr, v.ptr, v.step, v.cnt, lf.cnt));
        else
            emit(sfmt("mm_arr_copy_%s(%s, %s, %s, 1, %s, %s);",
                      sfx, v.ptr, v.step, lf.ptr, v.cnt, lf.cnt));
        return;
    }
    if (strcmp(op, "RANDOMIZE") == 0) {
        if (stmt_end()) {
            cv.uses_datetime = 1;
            emit("mm_randomize(mm_epoch_now());");
        }
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
/*
 * A variable, possibly an array element, that INPUT can write.
 *
 * *cap comes back as the LENGTH of a string array element and 0 for
 * everything else - a plain string has room for its NUL and takes
 * mm_sset.
 */
struct val input_target(int *cap)
{
    struct tok *t = nxt();
    struct sym *sym;
    struct val r;
    int is_arr;
    int sfx;
    const char *canon;

    *cap = 0;
    if (t->kind != T_ID)
        cv_err("INPUT needs a variable");
    canon = split_suffix(t->text, &sfx);
    /* INSIDE A FUNCTION, ITS OWN NAME IS A VARIABLE - the return
       value - and CAT, INC and INPUT write it like any other.
       Without this the write went to an invisible implied GLOBAL of
       the same name: PETSCII Robots' path$() builds its result with
       `Cat path$, "/" + f$`, so every file path came back as the
       bare directory and loadimage read a DIRECTORY as its BMP -
       "not a BMP file" with a file that was perfectly good. */
    if (cv.cur != NULL && cv.cur->is_func
        && strcmp(canon, cv.cur->name) == 0 && !is_op("(", 0)) {
        if (sfx != TY_NONE && sfx != cv.cur->ty)
            cv_err("'%s' is %s but used as %s", canon,
                   tyname_of(cv.cur->ty), tyname_of(sfx));
        r.code = retacc();
        r.ty = cv.cur->ty;
        return r;
    }
    is_arr = is_op("(", 0);
    sym = reference(t->text, 0);
    if (sym->is_const)
        cv_err("'%s' is a CONST", sym->name);
    if (is_arr) {
        if (!sym->is_array)
            cv_err("'%s' is not an array", sym->name);
        r.code = index_of(sym);
        r.ty = sym->ty;
        *cap = sym->alen;
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
        int cap;
        struct val tgt = input_target(&cap);
        if (tgt.ty == TY_S)
            emit(swrite_cap(cap, tgt.code, "mm_input_next()"));
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
    int cap;

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
    tgt = input_target(&cap);
    if (tgt.ty != TY_S)
        cv_err("LINE INPUT needs a string variable");
    emit(swrite_cap(cap, tgt.code, sfmt("mm_getline(%s)", chan)));
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
    struct calldisp *d;
    int sfx;

    if (t != NULL && t->kind == T_STR) {
        /* a literal name resolves here and now: a direct call */
        cv.i++;
        canon = split_suffix(lower(t->text), &sfx);
        r = routine_get(canon);
        if (r == NULL)
            cv_err("CALL to unknown subroutine '%s'", canon);
        accept_op(",");
        call_args(0, &args);
        v = emit_call(r, &args);
        emit(sfmt("%s;", v.code));
        return;
    }
    if (t != NULL && t->kind == T_ID
        && routine_get(split_suffix(t->text, &sfx)) != NULL) {
        /* the classic form: CALL subname [, args] */
        cv.i++;
        r = routine_get(split_suffix(t->text, &sfx));
        accept_op(",");
        call_args(0, &args);
        v = emit_call(r, &args);
        emit(sfmt("%s;", v.code));
        return;
    }
    /* the name is a run-time string: dispatch by name */
    v = expr();
    if (v.ty != TY_S)
        cv_err("CALL needs a SUB name or a string");
    args.n = 0;
    if (accept_op(",")) {
        while (1) {
            arg_item(&args.a[args.n++]);
            if (!accept_op(","))
                break;
        }
    }
    d = call_dispatch(0, &args);
    v = emit_call_byname(d, v.code, &args);
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
              tgt, as_int(start), has_num ? as_int(num) : "-1LL",
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
    struct shead sh;
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
            store("__ret", as_int(v), TY_I);
        } else {
            store("__ret", as_flt(v), TY_F);
        }
        return;
    }

    if (struct_head(t->text, &sh)) {
        const char *base = struct_base(sh.s);
        assign_member(member_path(base, sh.s->stype, sh.parts,
                                  sh.nparts, sh.sfx));
        return;
    }

    is_arr = is_op("(", 0);
    s = reference(t->text, 0);
    if (s->stype != NULL) {
        if (is_arr) {
            if (!s->is_array)
                cv_err("'%s' is not an array", canon);
            target = index_of(s);
        } else {
            if (s->is_array)
                cv_err("cannot assign to whole struct array '%s'",
                       canon);
            target = s->acc;
        }
        if (is_op(".", 0)) {
            assign_member(member_path(target, s->stype, NULL, 0,
                                      TY_NONE));
            return;
        }
        expect_op("=");
        assign_struct(target, s->stype);
        return;
    }
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
        emit(swrite_of(s, target, v.code));
    } else if (s->ty == TY_I) {
        store(target, as_int(v), TY_I);
    } else {
        store(target, as_flt(v), TY_F);
    }
}

/* A numeric assignment, guarded when the program uses ON ERROR.
 *
 * The interpreter never performs the assignment whose expression failed -
 * it jumps away before the store.  Here the expression has already run,
 * so the value goes to a temporary first and is committed only if the
 * statement survived.  Testing the flag before evaluating would be too
 * early: the flag is what evaluating sets.
 *
 * String assignment needs none of this: it goes through mm_sset, which
 * checks for itself. */
static void store(const char *target, const char *val, int ty)
{
    const char *ctype = (ty == TY_I) ? "MMINTEGER" : "MMFLOAT";
    char *tmp;

    if (!checks_on()) {
        emit(sfmt("%s = %s;", target, val));
        return;
    }
    tmp = newtmp("cv");
    emit(sfmt("{ %s %s = %s;", ctype, tmp, val));
    emit(sfmt("  if (!__mm_e[0]) %s = %s; }", target, tmp));
}

/* What a block header does when its own condition raised.
 *
 * The interpreter resumes at the textually next statement, so the answer
 * depends on the form the translator is looking at, which is the one
 * thing it knows and the interpreter does not have to: for a multi-line
 * IF or a loop the next statement is inside the body, so it is entered;
 * for a single-line IF the next statement is the next line, so the whole
 * statement is skipped. */
static char *poisoned_cond(const char *c, int enter)
{
    if (!checks_on())
        return (char *)c;
    if (enter)
        return sfmt("__mm_e[0] ? 1 : (%s)", c);
    return sfmt("!__mm_e[0] && (%s)", c);
}

/* -- IF -------------------------------------------------------------- */

static char *cond(void)
{
    struct val v = expr();

    if (v.ty == TY_S)
        cv_err("a string cannot be used as a condition");
    /* a comparison is already a truth value: wrapping it in "!= 0"
       made the backend compare the compare, every time the condition
       ran */
    if (boolean_expr(v.code))
        return (char *)v.code;
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
        /* block IF.  A condition that failed leaves the interpreter
           resuming at the textually next statement - which for a
           MULTI-LINE IF is the first statement of the THEN body.  So the
           poisoned condition is TRUE here, and false below. */
        emit(sfmt("if (%s) {", poisoned_cond(c, 1)));
        cv.indent++;
        push_block("if", NULL, 0);
        return;
    }
    /* single line IF: the next statement is the next LINE, so a failed
       condition skips the whole thing */
    emit(sfmt("if (%s) {", poisoned_cond(c, 0)));
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
        || strcmp(cv.blocks[cv.nblocks - 1].kind, kind) != 0) {
        /* A block whose OPENER could not be translated leaves its close
           with nothing to match, and "mismatched end of if block" then
           reads as a second, separate fault in a line that is perfectly
           good.  Say which line actually caused it - one real error and
           one consequence, not two mysteries. */
        if (cv.nskipped > 0)
            cv_err("end of %s block with no start - line %d above could "
                   "not be translated", kind,
                   cv.skipped[cv.nskipped - 1].line);
        cv_err("mismatched end of %s block", kind);
    }
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
    struct val step = { NULL, TY_NONE, NULL, 0 };
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
    if (cv.uses_onerror) {
        /* a lexical SKIP window never crosses into a routine body */
        cv.err_window = 0;
        cv.err_window_pending = -1;
        /* Entered with an error already recorded - which means an
           argument expression raised - so do nothing and go back.  The
           interpreter never gets here at all: it jumps away before the
           call.  Returning at once is the same thing observably, and it
           spends none of the skip count on statements in here. */
        emit(sfmt("if (__mm_e[0]) { %s%s }", routine_exit(),
                  r->is_func ? " return __ret;" : " return;"));
    }
    if (cv.uses_onerror)
        /* The SUB/FUNCTION line is itself a statement the interpreter
           executes and counts on every call, so entering costs one of
           the skip count.  Without this our count ran one statement
           further into a called routine than a real PicoMite's did. */
        emit("if (__mm_e[1]) { mm_pr_commit(); __mm_e[0] = 0;"
             " if (__mm_e[1] > 0) __mm_e[1]--; }");
    push_block("routine", NULL, 0);
}

static void emit_local_decl(struct sym *s)
{
    const char *pfx = s->is_static ? "static " : "";

    /* An array, a string or a structure that is not STATIC lives in
     * the invocation's heap block, declared once in its struct and
     * zeroed by mm_lheap; there is nothing to declare here. */
    if (!s->is_static && (s->is_array || s->ty == TY_S
                          || s->stype != NULL))
        return;

    if (s->is_static && s->has_init)
        emit(sfmt("static int __once_%s = 0;", dunder("", s->name)));
    if (s->is_array) {
        const char *dims = "";
        int k;
        for (k = 0; k < s->ndims; k++)
            dims = sfmt("%s[%s]", dims, s->dims[k]);
        if (s->ty == TY_S)
            emit(sfmt("%schar %s%s[%s];", pfx, s->acc, dims,
                      strsz_of(s)));
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
        if (p->stype != NULL) {
            part = sfmt("struct t_%s *%s", p->stype, nm);
            joined = joined ? sfmt("%s, %s", joined, part) : part;
            continue;
        }
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

/* The C prototype of a CALL-by-name dispatcher: the shape of its
 * representative routine with the name argument added. */
char *calld_head(struct calldisp *d)
{
    struct routine *rep = d->rep;
    const char *joined = NULL;
    const char *ret;
    int k;

    if (d->is_func && rep->ty == TY_S)
        joined = "char *__ret";
    {
        const char *part = "const char *__nm";

        joined = joined ? sfmt("%s, %s", joined, part) : part;
    }
    for (k = 0; k < d->nparams; k++) {
        struct sym *p = rep->params[k];
        char *nm = dunder("p_", p->name);
        const char *part;

        if (p->ty == TY_S)
            part = sfmt("char *%s", nm);
        else if (p->byref)
            part = sfmt("%s *%s", ctype_of(p->ty), nm);
        else
            part = sfmt("%s %s", ctype_of(p->ty), nm);
        joined = sfmt("%s, %s", joined, part);
    }
    if (!d->is_func)
        ret = "static void ";
    else if (rep->ty == TY_S)
        ret = "static char *";
    else
        ret = sfmt("static %s ", ctype_of(rep->ty));
    return sfmt("%s%s(%s)", ret, d->name, joined);
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

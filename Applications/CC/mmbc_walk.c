/* mmbc_walk.c - passes 2 and 3: walk every statement, scanning then
 * emitting.  Mirrors mmb2c.py walk/statement/skip_out/loop_cond
 * (1820-1911).  statement_inner and the do_* handlers live in
 * mmbc_stmt.c. */

#include "mmbc.h"

/* Python str.strip() - scratch copy with surrounding whitespace gone. */
static char *stripped(const char *s)
{
    const char *e;
    char *out;
    size_t n;

    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'
                     || e[-1] == '\n'))
        e--;
    n = (size_t)(e - s);
    out = salloc(n + 1);
    memcpy(out, s, n);
    out[n] = 0;
    return out;
}

void walk(int mode)
{
    volatile int idx;

    cv.mode = mode;
    cv.in_type = 0;
    cv.gosub_n = 0;
    cv.cur = NULL;
    cv.indent = 1;
    cv.nblocks = 0;
    cv.out = &cv.out_main;
    cv.opt_default = TY_F;
    cv.opt_explicit = 0;
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
        if (cv.ntoks == 0)
            continue;
        cv.i = 0;
        strip_line_number();
        while (!at_end()) {
            jmp_buf jb2, *saved2;
            if (accept_op(":"))
                continue;
            saved2 = err_jmp;
            err_jmp = &jb2;
            if (setjmp(jb2) == 0) {
                statement();
                err_jmp = saved2;
            } else {
                err_jmp = saved2;
                errors_add_dedup(err_msg);
                skip_statement();
            }
        }
    }
    if (cv.nblocks > 0)
        errors_add(sfmt("unterminated %s block (started line %d)",
                        cv.blocks[cv.nblocks - 1].kind,
                        cv.blocks[cv.nblocks - 1].line));
}

/* Undo whatever a failed statement emitted and leave a comment in its
 * place, so one untranslatable line does not lose the rest of the
 * program. */
static void skip_out(int where, struct outbuf *out_at_entry, int ind,
                     struct block *blocks_snap, int nblocks_snap,
                     int tok_at_entry, const char *why)
{
    char *text;
    const char *reason;

    if (cv.out == out_at_entry)
        out_at_entry->n = where;
    cv.indent = ind;
    memcpy(cv.blocks, blocks_snap,
           sizeof(struct block) * (size_t)nblocks_snap);
    cv.nblocks = nblocks_snap;
    skip_statement();
    text = stripped(source_text(tok_at_entry, cv.i));
    if (text[0] == 0)
        text = stripped(src_lines[cv.lineno - 1]);
    reason = why;
    if (strncmp(reason, "line ", 5) == 0) {
        const char *p = strstr(reason, ": ");
        if (p != NULL)
            reason = p + 2;
    }
    if (cv.mode == M_EMIT) {
        struct skip_rec *r;
        GROW(cv.skipped, cv.nskipped, cv.cskipped);
        r = &cv.skipped[cv.nskipped++];
        r->line = cv.lineno;
        r->text = pstr(text);
        r->why = pstr(reason);
        emit(sfmt("/* MMBASIC line %d not translated: %s */",
                  cv.lineno, cblock_safe(reason)));
        emit(sfmt("/*     %s */", cblock_safe(text)));
    }
}

/* Wrapper that gives every statement a clean string scratch stack.
 * String temporaries are only ever live inside one statement, so
 * winding the scratch stack back to the enclosing function's mark
 * before each statement keeps usage bounded no matter how long a loop
 * runs. */
void statement(void)
{
    int where = cv.out->n;
    struct outbuf *out_at_entry = cv.out;
    int ind = cv.indent;
    struct block *blocks_snap;
    int nblocks_snap = cv.nblocks;
    int tok_at_entry = cv.i;
    int outer = cv.tmp_used;
    volatile int failed = 0;
    char *volatile failmsg = NULL;
    jmp_buf jb, *saved;

    blocks_snap = salloc(sizeof(struct block) * (size_t)(nblocks_snap
                                                         ? nblocks_snap
                                                         : 1));
    memcpy(blocks_snap, cv.blocks,
           sizeof(struct block) * (size_t)nblocks_snap);
    cv.tmp_used = 0;
    saved = err_jmp;
    err_jmp = &jb;
    if (setjmp(jb) == 0) {
        statement_inner();
        err_jmp = saved;
    } else {
        err_jmp = saved;
        if (!cv.lenient) {
            /* the finally clause still runs before the re-raise -
             * failed is None on this path, exactly like the Python */
            if (cv.mode == M_EMIT && cv.tmp_used
                && cv.out == out_at_entry)
                out_insert(out_at_entry, where,
                           pstr(sfmt("%*smm_release(__mark);",
                                     ind * 4, "")));
            cv.tmp_used = outer || cv.tmp_used;
            mm_error("%s", err_msg);
        }
        failed = 1;
        failmsg = sstr(err_msg);
    }
    /* finally */
    if (cv.mode == M_EMIT && cv.tmp_used && cv.out == out_at_entry
        && !failed)
        out_insert(out_at_entry, where,
                   pstr(sfmt("%*smm_release(__mark);", ind * 4, "")));
    /* Clear the poison and count the statement, where the interpreter
     * does it: AFTER the statement (MMBasic.c:1867).  Before would count
     * a statement that calls a SUB ahead of the SUB's own statements, and
     * the count inside would be one short.  A statement that opened or
     * closed a block has emitted a brace by now, so its guard goes in
     * front instead - for an opener that is the same thing, and for a
     * closer it lands where the closing keyword executes anyway. */
    if (cv.mode == M_EMIT && cv.uses_onerror && cv.out == out_at_entry
        && !failed) {
        const char *guard =
            pstr(sfmt("%*sif (__mm_e[1]) { __mm_e[0] = 0;"
                      " if (__mm_e[1] > 0) __mm_e[1]--; }", ind * 4, ""));
        if (cv.nblocks == nblocks_snap && cv.out->n > where)
            out_append(out_at_entry, guard);
        else
            out_insert(out_at_entry, where, guard);
    }
    cv.tmp_used = outer || cv.tmp_used;
    if (failed)
        skip_out(where, out_at_entry, ind, blocks_snap, nblocks_snap,
                 tok_at_entry, failmsg);
}

/* A loop test is re-evaluated every time round, so it needs its own
 * release point. */
char *loop_cond(const char *c)
{
    return sfmt("(mm_release(__mark), (%s))", c);
}

/* mmb2c.py 2910 - shared by do_next and bound_of. */
int is_literal_number(struct val v)
{
    const char *p;

    for (p = v.code; *p; p++)
        if (!(is_digit_c(*p) || strchr("-+.()LlEe", *p) != NULL))
            return 0;
    return 1;
}

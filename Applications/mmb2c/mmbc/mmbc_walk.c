/* mmbc_walk.c - passes 2 and 3: walk every statement, scanning then
 * emitting.  Mirrors mmb2c.py walk/statement/skip_out/loop_cond
 * (1820-1911).  statement_inner and the do_* handlers live in
 * mmbc_stmt.c. */

#include "mmbc.h"

/* Are the ON ERROR checked forms live for the statement being emitted?
 * IGNORE (or an unknowable SKIP count) arms the whole program; a
 * literal SKIP n arms the ON ERROR statement and the next n.  MMBasic's
 * own counter is dynamic - it follows execution into a called SUB - so
 * a compiled ARITHMETIC error deeper in a callee than the routine-entry
 * decrements reach is not trapped where the interpreter would have
 * counted its way to it; a runtime command error is trapped anywhere,
 * because the runtime consults the armed count wherever it is raised. */
int checks_on(void)
{
    return cv.onerror_global || cv.err_window > 0;
}

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
    cv.mx_n[0] = cv.mx_n[1] = 0;
    cv.late_cut = -1;
    cv.gosub_n = 0;
    cv.cur = NULL;
    cv.indent = 1;
    cv.err_window = 0;
    cv.err_window_pending = -1;
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
/* A statement that hit an error the reference raises only when the line
 * runs (cv_late): whatever it emitted goes, it becomes that error where
 * it stands, and the rest of it is skipped - an ELSE kept.  Not one that
 * opened, closed or turned a block, or moved the output: that is noted,
 * and 0 sends it down the ordinary failure path, as the Python's
 * re-raise does. */
static int late_out(int where, struct outbuf *out_at_entry, int ind,
                    struct block *blocks_snap, int nblocks_snap,
                    int tok_at_entry)
{
    static const char *const words[] = {
        "IF", "ELSEIF", "ELSE", "ENDIF", "END", "FOR", "NEXT", "DO", "LOOP",
        "WHILE", "WEND", "SELECT", "CASE", "SUB", "FUNCTION", NULL
    };
    char msg[sizeof err_msg];
    const char *m = err_msg;
    int k, blockword = 0;

    err_late = 0;
    if (strncmp(m, "line ", 5) == 0 && strstr(m, ": ") != NULL)
        m = strstr(m, ": ") + 2;
    snprintf(msg, sizeof msg, "%s", m);
    if (tok_at_entry < cv.ntoks && cv.toks[tok_at_entry].kind == T_ID)
        for (k = 0; words[k] != NULL; k++)
            if (strcmp(cv.toks[tok_at_entry].up, words[k]) == 0)
                blockword = 1;
    if (cv.nblocks != nblocks_snap
        || memcmp(cv.blocks, blocks_snap,
                  sizeof(struct block) * (size_t)nblocks_snap) != 0
        || cv.out != out_at_entry || blockword) {
        /* OPTION EXPLICIT's "not declared" in a FOR, an IF, a SELECT - a
         * block that cannot simply be dropped - is translated again round
         * a stand-in for the name, behind the error: findvar fails only
         * if the line runs (FUZIX #25).  Other late errors here, and a
         * retry that fails, are refused. */
        static const char nd[] = " is not declared";
        size_t ml = strlen(msg);

        if (cv.out == out_at_entry
            && (cv.mode == M_SCAN || cv.mode == M_EMIT)
            && !cv.undecl_standin && ml >= sizeof nd - 1
            && strcmp(msg + ml - (sizeof nd - 1), nd) == 0) {
            jmp_buf jb2, *saved2 = err_jmp;
            volatile int ok = 0;

            out_at_entry->n = where;
            cv.indent = ind;
            memcpy(cv.blocks, blocks_snap,
                   sizeof(struct block) * (size_t)nblocks_snap);
            cv.nblocks = nblocks_snap;
            cv.i = tok_at_entry;
            cv.undecl_standin = 1;
            err_jmp = &jb2;
            if (setjmp(jb2) == 0) {
                statement_inner();
                ok = 1;
            }
            err_jmp = saved2;
            cv.undecl_standin = 0;
            if (!ok)
                return 0;       /* the retry's own error: refused */
            out_insert(out_at_entry, where,
                       pstr(sfmt("%*smm_error(\"%s\");", ind * 4, "", msg)));
            cv_warn("\"%s\" when the line runs", msg);
            return 1;
        }
        cv_note("%s", msg);
        return 0;
    }
    /* A DIM, LOCAL or STATIC with several names makes the ones before
     * the failing name, as findvar does one name at a time: their
     * allocation and initialiser stay (FUZIX #23).  Cutting the whole
     * statement left them declared and never allocated. */
    if (cv.late_cut >= 0 && cv.late_cut > where)
        where = cv.late_cut;
    out_at_entry->n = where;
    cv.indent = ind;
    while (!stmt_end())
        cv.i++;
    cv_warn("\"%s\" when the line runs", msg);
    emit(sfmt("mm_error(\"%s\");", msg));
    return 1;
}

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
    int mx_outer[2];
    int cut_outer;

    blocks_snap = salloc(sizeof(struct block) * (size_t)(nblocks_snap
                                                         ? nblocks_snap
                                                         : 1));
    memcpy(blocks_snap, cv.blocks,
           sizeof(struct block) * (size_t)nblocks_snap);
    cv.tmp_used = 0;
    /* MAX/MIN pairs are per statement: one statement's C is sequenced
     * against the next, so the numbering starts again.  The outer one's
     * is put back after a statement inside it (IF ... THEN). */
    mx_outer[0] = cv.mx_n[0];
    mx_outer[1] = cv.mx_n[1];
    cv.mx_n[0] = cv.mx_n[1] = 0;
    /* Where a late error cuts back to, when not to the statement's
     * start: a DIM with several names keeps the ones before (late_out) */
    cut_outer = cv.late_cut;
    cv.late_cut = -1;
    saved = err_jmp;
    err_jmp = &jb;
    if (setjmp(jb) == 0) {
        statement_inner();
        err_jmp = saved;
        cv.mx_n[0] = mx_outer[0];
        cv.mx_n[1] = mx_outer[1];
        cv.late_cut = cut_outer;
    } else if (err_late
               && late_out(where, out_at_entry, ind, blocks_snap,
                           nblocks_snap, tok_at_entry)) {
        /* the statement became the run-time error: carry on as one
           that was translated */
        err_jmp = saved;
        cv.mx_n[0] = mx_outer[0];
        cv.mx_n[1] = mx_outer[1];
        cv.late_cut = cut_outer;
    } else {
        err_jmp = saved;
        cv.mx_n[0] = mx_outer[0];
        cv.mx_n[1] = mx_outer[1];
        cv.late_cut = cut_outer;
        if (!cv.lenient) {
            /* the finally clause still runs before the re-raise -
             * failed is None on this path, exactly like the Python */
            if (cv.mode == M_EMIT && cv.tmp_used
                && cv.out == out_at_entry)
                out_insert(out_at_entry, where,
                           pstr(sfmt("%*smm_release(__mark);",
                                     ind * 4, "")));
            cv.tmp_used = outer || cv.tmp_used;
            {
                /* err_msg is mm_error's OWN destination, so passing it
                 * as the "%s" argument aliases vsnprintf's output onto
                 * its input: the re-raised message came out empty and
                 * `mmbc --strict` reported a blank line for every real
                 * error it refused.  Copy it clear first. */
                char again[sizeof err_msg];

                memcpy(again, err_msg, sizeof again);
                mm_error("%s", again);
            }
        }
        failed = 1;
        failmsg = sstr(err_msg);
    }
    /* finally */
    if (cv.mode == M_EMIT && cv.tmp_used && cv.out == out_at_entry
        && !failed)
        out_insert(out_at_entry, where,
                   pstr(sfmt("%*smm_release(__mark);", ind * 4, "")));
    /* A literal ON ERROR SKIP n arms its window here: the ON ERROR
     * statement itself is covered (n+1 with the runtime's own +1), so
     * ITS guard performs the decrement the interpreter does at the end
     * of the ON ERROR line, and the counter reaches the next statement
     * intact. */
    if (cv.err_window_pending >= 0 && !failed)
        cv.err_window = cv.err_window_pending + 1;
    cv.err_window_pending = -1;
    /* Clear the poison and count the statement, where the interpreter
     * does it: AFTER the statement (MMBasic.c:1867).  Before would count
     * a statement that calls a SUB ahead of the SUB's own statements, and
     * the count inside would be one short.  A statement that opened or
     * closed a block has emitted a brace by now, so its guard goes in
     * front instead - for an opener that is the same thing, and for a
     * closer it lands where the closing keyword executes anyway. */
    if (cv.mode == M_EMIT && checks_on() && cv.out == out_at_entry
        && !failed) {
        const char *guard =
            pstr(sfmt("%*sif (__mm_e[1]) mm_err_stmt();", ind * 4, ""));
        if (cv.nblocks == nblocks_snap && cv.out->n > where)
            out_append(out_at_entry, guard);
        else
            out_insert(out_at_entry, where, guard);
    }
    if (cv.err_window > 0 && !failed)
        cv.err_window--;
    /* The interrupt poll goes AFTER the error bookkeeping, which is the
     * interpreter's own order: statement, error bookkeeping, then
     * check_interrupt (MMBasic.c:1852-1879).  Same opener/closer
     * placement rule as the guard above. */
    if (cv.mode == M_EMIT && cv.uses_interrupts && cv.out == out_at_entry
        && !failed) {
        const char *poll =
            pstr(sfmt("%*sif (__mm_int_armed) mm_int_poll();",
                      ind * 4, ""));
        if (cv.nblocks == nblocks_snap && cv.out->n > where)
            out_append(out_at_entry, poll);
        else
            out_insert(out_at_entry, where, poll);
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

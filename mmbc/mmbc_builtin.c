/* mmbc_builtin.c - the built-in function machinery: call_builtin,
 * emit_builtin and builtin_raw.
 *
 * Mirrors mmb2c.py `def call_builtin` (934) through `def builtin_raw`
 * (1082-1326).  Where the Python evaluates its f()/n()/s() closures in
 * a fixed left-to-right order, the C sequences them into locals first -
 * C argument evaluation order is unspecified and a wrong order could
 * change which coercion error fires. */

#include "mmbc.h"
#include "mmbc_expr.h"

static struct val mkval(const char *code, int ty)
{
    struct val v;
    v.code = code;
    v.ty = ty;
    return v;
}

struct val call_builtin(const char *up)
{
    const struct builtin *b;
    struct val args[MAXARGS];
    int nargs = 0;

    if (rawarg_in(up))
        return builtin_raw(up);
    b = builtin_get(up);
    if (is_op("(", 0)) {
        cv.i += 1;
        if (!accept_op(")")) {
            for (;;) {
                if (nargs >= MAXARGS)
                    mm_error("line %d: too many arguments", cv.lineno);
                args[nargs++] = expr();
                if (!accept_op(","))
                    break;
            }
            expect_op(")");
        }
    }
    if (nargs < b->minargs || nargs > b->maxargs)
        cv_err("%s() takes %d..%d argument(s), %d given",
               up, b->minargs, b->maxargs, nargs);
    if (strfunc_in(up))
        cv.tmp_used = 1;
    return emit_builtin(up, args, nargs);
}

/* The Python's f(k)/n(k)/s(k) closures over (up, args). */

static const char *bi_f(struct val *args, int k)
{
    return as_flt(args[k]);
}

static const char *bi_n(struct val *args, int k)
{
    return as_int(args[k]);
}

static const char *bi_s(const char *up, struct val *args, int k)
{
    if (args[k].ty != TY_S)
        cv_err("%s() expects a string argument", up);
    return args[k].code;
}

struct val emit_builtin(const char *up, struct val *args, int nargs)
{
#define f(k) bi_f(args, k)
#define n(k) bi_n(args, k)
#define s(k) bi_s(up, args, k)

    if (strcmp(up, "ABS") == 0) {
        if (args[0].ty == TY_I)
            return mkval(sfmt("(MMINTEGER)llabs((long long)(%s))",
                              args[0].code), TY_I);
        return mkval(sfmt("fabs(%s)", f(0)), TY_F);
    }
    if (strcmp(up, "INT") == 0) {
        if (args[0].ty == TY_I)
            return mkval(args[0].code, TY_I);
        return mkval(sfmt("(MMINTEGER)mm_int(%s)", f(0)), TY_I);
    }
    if (strcmp(up, "FIX") == 0) {
        if (args[0].ty == TY_I)
            return mkval(args[0].code, TY_I);
        return mkval(sfmt("(MMINTEGER)mm_fix(%s)", f(0)), TY_I);
    }
    if (strcmp(up, "CINT") == 0)
        return mkval(sfmt("mm_toint(%s)", f(0)), TY_I);
    if (strcmp(up, "SGN") == 0)
        return mkval(sfmt("mm_sgn(%s)", f(0)), TY_I);
    {
        static const struct { const char *name; const char *cf; } m[] = {
            { "SQR", "sqrt" }, { "SIN", "sin" }, { "COS", "cos" },
            { "TAN", "tan" }, { "ATN", "atan" }, { "LOG", "log" },
            { "EXP", "exp" }, { "ASIN", "asin" }, { "ACOS", "acos" },
            { NULL, NULL }
        };
        int k;

        for (k = 0; m[k].name; k++)
            if (strcmp(up, m[k].name) == 0)
                return mkval(sfmt("%s(%s)", m[k].cf, f(0)), TY_F);
    }
    if (strcmp(up, "ATAN2") == 0) {
        const char *a0 = f(0);
        const char *a1 = f(1);

        return mkval(sfmt("atan2(%s, %s)", a0, a1), TY_F);
    }
    if (strcmp(up, "DEG") == 0)
        return mkval(sfmt("((%s) * (180.0 / 3.14159265358979323846))",
                          f(0)), TY_F);
    if (strcmp(up, "RAD") == 0)
        return mkval(sfmt("((%s) * (3.14159265358979323846 / 180.0))",
                          f(0)), TY_F);
    if (strcmp(up, "RND") == 0)
        return mkval("mm_rnd()", TY_F);
    if (strcmp(up, "PI") == 0)
        return mkval("3.14159265358979323846", TY_F);
    if (strcmp(up, "MAX") == 0 || strcmp(up, "MIN") == 0) {
        const char *cop = strcmp(up, "MAX") == 0 ? ">" : "<";
        int allint = 1;
        int ty;
        const char *cur;
        int k;

        for (k = 0; k < nargs; k++)
            if (args[k].ty != TY_I)
                allint = 0;
        ty = allint ? TY_I : TY_F;
        cur = allint ? args[0].code : as_flt(args[0]);
        for (k = 1; k < nargs; k++) {
            const char *o = allint ? args[k].code : as_flt(args[k]);

            cur = sfmt("((%s) %s (%s) ? (%s) : (%s))", cur, cop, o, cur, o);
        }
        return mkval(cur, ty);
    }
    if (strcmp(up, "BIT") == 0) {
        const char *a0 = n(0);
        const char *a1 = n(1);

        return mkval(sfmt("(((%s) >> (%s)) & 1LL)", a0, a1), TY_I);
    }
    if (strcmp(up, "LEN") == 0)
        return mkval(sfmt("(MMINTEGER)mm_slen(%s)", s(0)), TY_I);
    if (strcmp(up, "ASC") == 0)
        return mkval(sfmt("mm_asc(%s)", s(0)), TY_I);
    if (strcmp(up, "BYTE") == 0) {
        const char *a0 = s(0);
        const char *a1 = n(1);

        return mkval(sfmt("mm_byte(%s, %s)", a0, a1), TY_I);
    }
    if (strcmp(up, "VAL") == 0)
        return mkval(sfmt("mm_val(%s)", s(0)), TY_F);
    if (strcmp(up, "INSTR") == 0) {
        if (nargs == 2) {
            const char *a0 = s(0);
            const char *a1 = s(1);

            return mkval(sfmt("mm_instr(1, %s, %s)", a0, a1), TY_I);
        }
        {
            const char *a0 = n(0);
            const char *a1 = s(1);
            const char *a2 = s(2);

            return mkval(sfmt("mm_instr(%s, %s, %s)", a0, a1, a2), TY_I);
        }
    }
    if (strcmp(up, "TAB") == 0)
        return mkval(sfmt("mm_tab(%s)", n(0)), TY_S);
    if (strcmp(up, "TIMER") == 0)
        return mkval("mm_timer()", TY_F);
    if (strcmp(up, "DATE$") == 0)
        return mkval("mm_date_str()", TY_S);
    if (strcmp(up, "TIME$") == 0)
        return mkval("mm_time_str()", TY_S);
    if (strcmp(up, "CWD$") == 0)
        return mkval("mm_cwd()", TY_S);
    if (strcmp(up, "CHR$") == 0)
        return mkval(sfmt("mm_chr(%s)", n(0)), TY_S);
    if (strcmp(up, "LEFT$") == 0) {
        const char *a0 = s(0);
        const char *a1 = n(1);

        return mkval(sfmt("mm_left(%s, %s)", a0, a1), TY_S);
    }
    if (strcmp(up, "RIGHT$") == 0) {
        const char *a0 = s(0);
        const char *a1 = n(1);

        return mkval(sfmt("mm_right(%s, %s)", a0, a1), TY_S);
    }
    if (strcmp(up, "MID$") == 0) {
        if (nargs == 2) {
            const char *a0 = s(0);
            const char *a1 = n(1);

            return mkval(sfmt("mm_mid(%s, %s, -1)", a0, a1), TY_S);
        }
        {
            const char *a0 = s(0);
            const char *a1 = n(1);
            const char *a2 = n(2);

            return mkval(sfmt("mm_mid(%s, %s, %s)", a0, a1, a2), TY_S);
        }
    }
    if (strcmp(up, "STR$") == 0) {
        const char *m2 = (nargs > 1) ? n(1) : "0";
        const char *nn = (nargs > 2) ? n(2) : "MM_AUTO_PRECISION";
        const char *pad = (nargs > 3) ? s(3) : "\"\\001\" \" \"";

        if (args[0].ty == TY_I)
            return mkval(sfmt("mm_str_i(%s, %s, %s, %s)",
                              args[0].code, m2, nn, pad), TY_S);
        return mkval(sfmt("mm_str_f(%s, %s, %s, %s)",
                          f(0), m2, nn, pad), TY_S);
    }
    if (strcmp(up, "FORMAT$") == 0) {
        const char *fmt = (nargs > 1) ? s(1) : "\"\\002\" \"%g\"";
        const char *a0 = f(0);

        return mkval(sfmt("mm_format(%s, %s)", a0, fmt), TY_S);
    }
    if (strcmp(up, "HEX$") == 0 || strcmp(up, "OCT$") == 0
        || strcmp(up, "BIN$") == 0) {
        const char *cf = strcmp(up, "HEX$") == 0 ? "mm_hex"
            : strcmp(up, "OCT$") == 0 ? "mm_oct" : "mm_bin";
        const char *w = (nargs > 1) ? n(1) : "0";
        const char *a0 = n(0);

        return mkval(sfmt("%s(%s, %s)", cf, a0, w), TY_S);
    }
    if (strcmp(up, "UCASE$") == 0)
        return mkval(sfmt("mm_ucase(%s)", s(0)), TY_S);
    if (strcmp(up, "LCASE$") == 0)
        return mkval(sfmt("mm_lcase(%s)", s(0)), TY_S);
    if (strcmp(up, "LTRIM$") == 0)
        return mkval(sfmt("mm_ltrim(%s)", s(0)), TY_S);
    if (strcmp(up, "RTRIM$") == 0)
        return mkval(sfmt("mm_rtrim(%s)", s(0)), TY_S);
    if (strcmp(up, "SPACE$") == 0)
        return mkval(sfmt("mm_space(%s)", n(0)), TY_S);
    if (strcmp(up, "STRING$") == 0) {
        struct val a1 = args[1];
        const char *ch = (a1.ty == TY_S) ? sfmt("mm_asc(%s)", a1.code)
                                         : n(1);
        const char *a0 = n(0);

        return mkval(sfmt("mm_strrep(%s, %s)", a0, ch), TY_S);
    }
    if (strcmp(up, "FIELD$") == 0) {
        const char *delim = (nargs > 2) ? s(2) : "\"\\001\" \",\"";
        const char *quote = (nargs > 3) ? s(3) : "\"\\000\" \"\"";
        const char *a0 = s(0);
        const char *a1 = n(1);

        return mkval(sfmt("mm_field(%s, %s, %s, %s)", a0, a1, delim, quote),
                     TY_S);
    }
    if (strcmp(up, "MM.HRES") == 0)
        return mkval("mm_hres()", TY_I);
    if (strcmp(up, "MM.VRES") == 0)
        return mkval("mm_vres()", TY_I);
    if (strcmp(up, "PIXEL") == 0) {
        /* PIXEL(x, y) reads a pixel back AS RGB888 - the kernel
           primitive maps the mode's own colour numbering back out,
           so nothing here knows about depths or palettes. */
        const char *a0 = n(0);
        const char *a1 = n(1);

        return mkval(sfmt("mm_pixel_get(%s, %s)", a0, a1), TY_I);
    }
    cv_err("built-in %s() is not supported yet", up);
    return mkval(NULL, TY_NONE);        /* not reached */

#undef f
#undef n
#undef s
}

/* -- built-ins whose arguments are not ordinary expressions ---------- */
struct val builtin_raw(const char *up)
{
    if (strfunc_in(up))
        cv.tmp_used = 1;
    if (strcmp(up, "CHOICE") == 0) {
        struct val c;
        struct val a;
        struct val b;

        expect_op("(");
        c = expr();
        if (c.ty == TY_S)
            cv_err("CHOICE() condition must be a number");
        expect_op(",");
        a = expr();
        expect_op(",");
        b = expr();
        expect_op(")");
        if ((a.ty == TY_S) != (b.ty == TY_S))
            cv_err("CHOICE() branches must be the same kind");
        if (a.ty == TY_S)
            return mkval(sfmt("((%s) != 0 ? (char *)(%s) : (char *)(%s))",
                              c.code, a.code, b.code), TY_S);
        if (a.ty == TY_I && b.ty == TY_I)
            return mkval(sfmt("((%s) != 0 ? (%s) : (%s))",
                              c.code, a.code, b.code), TY_I);
        {
            const char *fa = as_flt(a);
            const char *fb = as_flt(b);

            return mkval(sfmt("((%s) != 0 ? (%s) : (%s))", c.code, fa, fb),
                         TY_F);
        }
    }

    if (strcmp(up, "BOUND") == 0) {
        struct tok *t;
        struct sym *sym;
        struct val dim = mkval(NULL, TY_NONE);
        int has_dim = 0;

        expect_op("(");
        t = nxt();
        if (t->kind != T_ID)
            cv_err("BOUND() needs an array name");
        sym = reference(t->text, 1);
        expect_op("(");
        expect_op(")");
        if (accept_op(",")) {
            dim = expr();
            has_dim = 1;
        }
        expect_op(")");
        if (!sym->is_array)
            cv_err("'%s' is not an array", sym->name);
        return mkval(bound_of(sym, dim, has_dim), TY_I);
    }

    if (strcmp(up, "TRIM$") == 0) {
        struct val src;
        const char *mask = "\"\\001\" \" \"";
        const char *where = "'L'";

        expect_op("(");
        src = expr();
        if (src.ty != TY_S)
            cv_err("TRIM$() needs a string");
        if (accept_op(",")) {
            struct val m = expr();

            if (m.ty != TY_S)
                cv_err("TRIM$() mask must be a string");
            mask = m.code;
            if (accept_op(",")) {
                struct tok *t = peek(0);

                if (t != NULL && t->kind == T_ID
                    && (strcmp(t->up, "L") == 0 || strcmp(t->up, "R") == 0
                        || strcmp(t->up, "B") == 0)) {
                    where = sfmt("'%s'", t->up);
                    cv.i += 1;
                } else {
                    struct val w = expr();

                    if (w.ty != TY_S)
                        cv_err("TRIM$() 'where' must be L, R or B");
                    where = sfmt("(mm_slen(%s) ? %s[1] : 0)",
                                 w.code, w.code);
                }
            }
        }
        expect_op(")");
        return mkval(sfmt("mm_trim(%s, %s, %s)", src.code, mask, where),
                     TY_S);
    }

    if (strcmp(up, "DATETIME$") == 0 || strcmp(up, "DAY$") == 0
        || strcmp(up, "EPOCH") == 0) {
        const char *arg = NULL;

        expect_op("(");
        if (is_kw("NOW", 0)) {
            cv.i += 1;
            arg = "mm_epoch_now()";
        } else {
            struct val v = expr();

            if (v.ty == TY_S)
                arg = sfmt("mm_epoch_str(%s)", v.code);
            else if (strcmp(up, "DATETIME$") == 0)
                arg = as_int(v);
            else
                cv_err("%s() needs a date string or NOW", up);
        }
        expect_op(")");
        if (strcmp(up, "DATETIME$") == 0)
            return mkval(sfmt("mm_datetime(%s)", arg), TY_S);
        if (strcmp(up, "DAY$") == 0)
            return mkval(sfmt("mm_day(%s)", arg), TY_S);
        return mkval(sfmt("(%s)", arg), TY_I);
    }

    if (strcmp(up, "BIN2STR$") == 0 || strcmp(up, "STR2BIN") == 0) {
        struct tok *t;
        const char *tyname;
        struct val v;
        const char *big = "0";
        const char *konst;
        int isflt;

        expect_op("(");
        t = nxt();
        if (t->kind != T_ID || bintype_index(t->up) < 0)
            cv_err("%s() needs a type such as INT32 or DOUBLE", up);
        tyname = t->up;
        expect_op(",");
        v = expr();
        if (accept_op(",")) {
            struct tok *b = nxt();

            if (b->kind != T_ID || strcmp(b->up, "BIG") != 0)
                cv_err("%s() third argument must be BIG", up);
            big = "1";
        }
        expect_op(")");
        konst = sfmt("MM_B_%s", tyname);
        isflt = strcmp(tyname, "SINGLE") == 0
            || strcmp(tyname, "DOUBLE") == 0;
        if (strcmp(up, "BIN2STR$") == 0) {
            if (isflt)
                return mkval(sfmt("mm_bin2str(%s, %s, 0, %s)",
                                  konst, as_flt(v), big), TY_S);
            return mkval(sfmt("mm_bin2str(%s, 0.0, %s, %s)",
                              konst, as_int(v), big), TY_S);
        }
        if (v.ty != TY_S)
            cv_err("STR2BIN() needs a string");
        if (isflt)
            return mkval(sfmt("mm_str2bin_f(%s, %s, %s)",
                              konst, v.code, big), TY_F);
        return mkval(sfmt("mm_str2bin_i(%s, %s, %s)",
                          konst, v.code, big), TY_I);
    }

    if (strcmp(up, "RGB") == 0) {
        struct tok *t;
        struct val r;
        struct val g;
        struct val b;
        const char *ri;
        const char *gi;
        const char *bi;

        expect_op("(");
        t = peek(0);
        if (t != NULL && t->kind == T_ID && rgbname_get(t->up) >= 0
            && is_op(")", 1)) {
            long val = rgbname_get(t->up);

            cv.i += 2;
            return mkval(sfmt("0x%06lXLL", (unsigned long)val), TY_I);
        }
        r = expr();
        expect_op(",");
        g = expr();
        expect_op(",");
        b = expr();
        expect_op(")");
        ri = as_int(r);
        gi = as_int(g);
        bi = as_int(b);
        return mkval(sfmt("((((%s) & 0xFF) << 16) | (((%s) & 0xFF) << 8) "
                          "| ((%s) & 0xFF))", ri, gi, bi), TY_I);
    }

    if (strcmp(up, "LLEN") == 0 || strcmp(up, "LGETSTR$") == 0
        || strcmp(up, "LGETBYTE") == 0 || strcmp(up, "LINSTR") == 0
        || strcmp(up, "LCOMPARE") == 0 || strcmp(up, "LINPUT") == 0) {
        struct flat ls;
        struct val a;
        struct val b;

        expect_op("(");
        ls = lsref();
        if (strcmp(up, "LLEN") == 0) {
            expect_op(")");
            return mkval(sfmt("mm_ls_len(%s)", ls.ptr), TY_I);
        }
        if (strcmp(up, "LCOMPARE") == 0) {
            struct flat ls2;

            expect_op(",");
            ls2 = lsref();
            expect_op(")");
            return mkval(sfmt("mm_ls_compare(%s, %s)", ls.ptr, ls2.ptr),
                         TY_I);
        }
        expect_op(",");
        a = expr();
        if (strcmp(up, "LGETBYTE") == 0) {
            expect_op(")");
            return mkval(sfmt("mm_ls_getbyte(%s, %s, %d)",
                              ls.ptr, as_int(a), cv.opt_base), TY_I);
        }
        if (strcmp(up, "LINSTR") == 0) {
            const char *st = "1";

            if (a.ty != TY_S)
                cv_err("LINSTR needs a normal string to search for");
            if (accept_op(","))
                st = as_int(expr());
            expect_op(")");
            return mkval(sfmt("mm_ls_instr(%s, %s, %s)", ls.ptr, a.code, st),
                         TY_I);
        }
        expect_op(",");
        b = expr();
        expect_op(")");
        if (strcmp(up, "LGETSTR$") == 0) {
            const char *ai = as_int(a);
            const char *bi = as_int(b);

            return mkval(sfmt("mm_ls_getstr(%s, %s, %s)", ls.ptr, ai, bi),
                         TY_S);
        }
        {
            const char *ai = as_int(a);
            const char *bi = as_int(b);

            return mkval(sfmt("mm_ls_input(%s, %s, %s, %s)",
                              ls.ptr, ls.cnt, ai, bi), TY_I);
        }
    }

    if (strcmp(up, "EOF") == 0 || strcmp(up, "LOC") == 0
        || strcmp(up, "LOF") == 0) {
        const char *fn;
        const char *cf;

        expect_op("(");
        fn = channel();
        expect_op(")");
        cf = strcmp(up, "EOF") == 0 ? "mm_eof"
            : strcmp(up, "LOC") == 0 ? "mm_loc" : "mm_lof";
        return mkval(sfmt("%s(%s)", cf, fn), TY_I);
    }

    if (strcmp(up, "INPUT$") == 0) {
        struct val nbr;
        const char *fn;

        expect_op("(");
        nbr = expr();
        expect_op(",");
        fn = channel();
        expect_op(")");
        return mkval(sfmt("mm_input_str(%s, %s)", as_int(nbr), fn), TY_S);
    }

    if (strcmp(up, "DIR$") == 0) {
        struct val spec;
        const char *kind = "MM_DIR_FILE";

        expect_op("(");
        if (accept_op(")"))
            /* DIR$() with no arguments continues the previous search */
            return mkval("mm_dir(\"\\000\" \"\", 0, 0)", TY_S);
        spec = expr();
        if (spec.ty != TY_S)
            cv_err("DIR$() needs a file specification string");
        if (accept_op(",")) {
            struct tok *t = nxt();

            if (t->kind != T_ID || (strcmp(t->up, "ALL") != 0
                                    && strcmp(t->up, "DIR") != 0
                                    && strcmp(t->up, "FILE") != 0))
                cv_err("DIR$() type must be ALL, DIR or FILE");
            kind = sfmt("MM_DIR_%s", t->up);
        }
        expect_op(")");
        return mkval(sfmt("mm_dir(%s, %s, 1)", spec.code, kind), TY_S);
    }

    if (strcmp(up, "MATH") == 0) {
        struct tok *t;

        expect_op("(");
        t = nxt();
        if (t->kind == T_ID && matharray_in(t->up)) {
            const char *name = t->up;
            struct sym *sym = arrayref(1);
            struct flat fl;
            const char *sfx;
            const char *idx = "NULL";
            const char *fn;

            if (sym->ty == TY_S)
                cv_err("MATH(%s ...) needs a numeric array", name);
            fl = array_flat(sym);
            sfx = (sym->ty == TY_I) ? "i" : "f";
            if ((strcmp(name, "MAX") == 0 || strcmp(name, "MIN") == 0)
                && accept_op(",")) {
                struct tok *iv = nxt();
                struct sym *isym;

                if (iv->kind != T_ID)
                    cv_err("MATH(%s) index must be an integer variable",
                           name);
                isym = reference(iv->text, 0);
                if (isym->ty != TY_I || isym->is_array)
                    cv_err("MATH(%s) index must be an integer variable",
                           name);
                idx = sfmt("&%s", isym->acc);
            }
            expect_op(")");
            fn = strcmp(name, "SUM") == 0 ? "sum"
                : strcmp(name, "MEAN") == 0 ? "mean"
                : strcmp(name, "SD") == 0 ? "sd"
                : strcmp(name, "MAX") == 0 ? "max"
                : strcmp(name, "MIN") == 0 ? "min" : "med";
            if (strcmp(name, "MAX") == 0 || strcmp(name, "MIN") == 0)
                return mkval(sfmt("mm_st_%s_%s(%s, %s, %s)",
                                  fn, sfx, fl.ptr, fl.cnt, idx), TY_F);
            return mkval(sfmt("mm_st_%s_%s(%s, %s)",
                              fn, sfx, fl.ptr, fl.cnt), TY_F);
        }
        if (t->kind != T_ID || mathfunc_get(t->up) == 0)
            /* the joined lists = ', '.join(sorted(MATHFUNCS)) and
             * ', '.join(MATHARRAY) - keep in step with mmbc_tab.c */
            cv_err("MATH(%s ...) is not supported; translated are "
                   "%s and the array reductions %s", t->text,
                   "ATAN3, COSH, LOG10, SINH, TANH",
                   "SUM, MEAN, SD, MAX, MIN, MEDIAN");
        {
            const char *name = t->up;
            struct val a = expr();
            struct val b = mkval(NULL, TY_NONE);
            const char *cf;

            if (mathfunc_get(name) == 2) {
                expect_op(",");
                b = expr();
            }
            expect_op(")");
            if (strcmp(name, "ATAN3") == 0) {
                const char *fa = as_flt(a);
                const char *fb = as_flt(b);

                return mkval(sfmt("mm_atan3(%s, %s)", fa, fb), TY_F);
            }
            cf = strcmp(name, "COSH") == 0 ? "cosh"
                : strcmp(name, "SINH") == 0 ? "sinh"
                : strcmp(name, "TANH") == 0 ? "tanh" : "log10";
            return mkval(sfmt("%s(%s)", cf, as_flt(a)), TY_F);
        }
    }

    cv_err("built-in %s() is not supported yet", up);
    return mkval(NULL, TY_NONE);        /* not reached */
}

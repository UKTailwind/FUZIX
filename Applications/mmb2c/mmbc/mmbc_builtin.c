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

/* MATH(CRCn v [,length [,poly [,start [,end [,revIn [,revOut]]]]]]).
 * mmb2c.py's CRCWIDTH table: width -> default polynomial.  Returns 0
 * for a name that is not one of the four. */
static int crc_width(const char *up)
{
    if (strcmp(up, "CRC8") == 0)
        return 8;
    if (strcmp(up, "CRC12") == 0)
        return 12;
    if (strcmp(up, "CRC16") == 0)
        return 16;
    if (strcmp(up, "CRC32") == 0)
        return 32;
    return 0;
}

static const char *crc_poly(int bits)
{
    switch (bits) {
    case 8:
        return "0x07";
    case 12:
        return "0x80D";
    case 16:
        return "0x1021";
    default:
        return "0x04C11DB7";
    }
}

/* mmb2c.py `def do_math_crc`.  Any of the six optional arguments may be
 * written EMPTY to take its default - `MATH(CRC16 a(), , , &HFFFF)` -
 * which is what MMBasic's `if (argc > 3 && *argv[4])` amounts to
 * (MATHS.c:3128-3138), and programs do write it.  The defaults are
 * emitted as literals, so a program that passes none of them pays
 * nothing for them.
 *
 * The engine and the three places it deliberately differs from
 * PicoMite 6.03.00 are in mmb_crc.h. */
static struct val do_math_crc(const char *name)
{
    int bits = crc_width(name);
    const char *args[6];
    const char *fn, *src;
    int k;

    if (is_array_arg()) {
        struct sym *sym = arrayref(1);
        struct flat fl;

        if (sym->ty == TY_S)
            cv_err("MATH(%s ...) wants a number array or a string",
                   name);
        fl = array_flat(sym);
        fn = (sym->ty == TY_I) ? "mmg_crc_i" : "mmg_crc_f";
        src = sfmt("%s, %s", fl.ptr, fl.cnt);
    } else {
        struct val a = expr();

        if (a.ty != TY_S)
            cv_err("MATH(%s ...) wants a number array or a string",
                   name);
        fn = "mmg_crc_s";
        src = a.code;
    }
    args[0] = "0";
    args[1] = crc_poly(bits);
    args[2] = "0";
    args[3] = "0";
    args[4] = "0";
    args[5] = "0";
    for (k = 0; k < 6; k++) {
        if (!accept_op(","))
            break;
        if (is_op(",", 0) || is_op(")", 0))
            continue;               /* an empty slot keeps the default */
        args[k] = as_int(expr());
    }
    expect_op(")");
    cv.uses_crc = 1;
    return mkval(sfmt("%s(%d, %s, %s, %s, %s, %s, %s, %s)", fn, bits,
                      src, args[0], args[1], args[2], args[3], args[4],
                      args[5]), TY_I);
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

/* Just inside a call's '(': is the list ONE argument - no comma at its
 * own depth before its ')'?  0 if the ')' never comes. */
static int one_arg(void)
{
    int k, depth = 0;

    for (k = 0;; k++) {
        struct tok *t = peek(k);

        if (t == NULL)
            return 0;
        if (t->kind != T_OP)
            continue;
        if (strcmp(t->text, "(") == 0)
            depth++;
        else if (strcmp(t->text, ")") == 0) {
            if (depth == 0)
                return 1;
            depth--;
        } else if (strcmp(t->text, ",") == 0 && depth == 0)
            return 0;
    }
}

/* s with \ and " escaped, for a C string literal - the Python's two
 * replace() calls, in the same order. */
static const char *c_escape(const char *s)
{
    size_t n = strlen(s), i, j = 0;
    char *o = palloc(2 * n + 1);

    for (i = 0; i < n; i++) {
        if (s[i] == '\\' || s[i] == '"')
            o[j++] = '\\';
        o[j++] = s[i];
    }
    o[j] = 0;
    return o;
}

/* True when emitted C calls something: a name followed by '('.  A cast's
 * ')(' is not a call and an index is '['.  A '(' inside a string literal
 * counts too, which only errs on the safe side. */
static int has_call(const char *text)
{
    int i;

    if (text[0] == 0)
        return 0;
    for (i = 1; text[i] != 0; i++)
        if (text[i] == '(') {
            int c = (unsigned char)text[i - 1];

            if (c == '_' || is_digit_c(c) || (c >= 'a' && c <= 'z')
                || (c >= 'A' && c <= 'Z'))
                return 1;
        }
    return 0;
}

/* The value of an integer literal's text - 5LL, (5LL), (-(5LL)) - in
 * *out; 0 for anything else. */
int int_literal(const char *code, long long *out)
{
    const char *s = code;
    const char *e = code + strlen(code);
    int neg = 0;
    long long v = 0;
    const char *p;

    for (;;) {
        while (e - s >= 2 && *s == '(' && e[-1] == ')') {
            int depth = 0;

            for (p = s + 1; p < e - 1; p++) {
                if (*p == '(')
                    depth++;
                else if (*p == ')' && --depth < 0)
                    break;
            }
            if (depth != 0)
                break;
            s++;
            e--;
        }
        if (s == e || *s != '-')
            break;
        neg = !neg;
        s++;
    }
    if (e - s < 3 || e[-2] != 'L' || e[-1] != 'L' || e - s - 2 > 18)
        return 0;
    for (p = s; p < e - 2; p++) {
        if (!is_digit_c((unsigned char)*p))
            return 0;
        v = v * 10 + (*p - '0');
    }
    *out = neg ? -v : v;
    return 1;
}

/* A numeric literal's value as the float getnumber makes of it: an
 * integer as int_literal reads it, or a float's digits, point and
 * exponent inside the parentheses and negations written round them.  0
 * for anything else. */
int literal_value(const char *code, double *out)
{
    const char *s = code;
    const char *e = code + strlen(code);
    const char *p;
    int neg = 0, mant = 0;
    long long iv;
    char buf[64];
    double v;

    if (int_literal(code, &iv)) {
        *out = (double)iv;
        return 1;
    }
    for (;;) {
        while (e - s >= 2 && *s == '(' && e[-1] == ')') {
            int depth = 0;

            for (p = s + 1; p < e - 1; p++) {
                if (*p == '(')
                    depth++;
                else if (*p == ')' && --depth < 0)
                    break;
            }
            if (depth != 0)
                break;
            s++;
            e--;
        }
        if (s == e || *s != '-')
            break;
        neg = !neg;
        s++;
    }
    /* digits [. digits] [e [+-] digits], at least one mantissa digit */
    p = s;
    while (p < e && is_digit_c((unsigned char)*p)) {
        p++;
        mant++;
    }
    if (p < e && *p == '.') {
        p++;
        while (p < e && is_digit_c((unsigned char)*p)) {
            p++;
            mant++;
        }
    }
    if (mant == 0)
        return 0;
    if (p < e && (*p == 'e' || *p == 'E')) {
        p++;
        if (p < e && (*p == '+' || *p == '-'))
            p++;
        if (p >= e || !is_digit_c((unsigned char)*p))
            return 0;
        while (p < e && is_digit_c((unsigned char)*p))
            p++;
    }
    if (p != e || e - s >= (long)sizeof buf)
        return 0;
    memcpy(buf, s, (size_t)(e - s));
    buf[e - s] = 0;
    v = strtod(buf, NULL);
    *out = neg ? -v : v;
    return 1;
}

/* MAX()/MIN() as fun_max_min: from the first value, each one replaces it
 * only when strictly greater (smaller), so a tie keeps the earlier, and
 * the result is always a float.
 *
 * Integer arguments are compared as integers and the answer made a float
 * once: the conversion never reorders two values, so that is the value
 * converting each and comparing floats gives, at one conversion instead
 * of one an argument.  A list of literals folds to the winning one,
 * integers compared as the floats getnumber makes of them, so a CONST or
 * a DATA item of it stays a constant.
 *
 * Each argument is evaluated once and in order.  Repeating the text in
 * nested ?: evaluated an argument up to 2^(n-1) times, so MAX(RND, 0.5)
 * could be below 0.5.  Text that calls nothing cannot tell how often it
 * is evaluated, so a short list of such arguments keeps the ?: chain, and
 * anything else goes through two temporaries of the enclosing routine,
 * declared by its scan.  Each MAX or MIN in a statement has a pair of its
 * own (mx_n): two sharing one in a single expression wrote it
 * unsequenced, which C leaves undefined. */
static struct val max_min(int is_max, struct val *args, int n)
{
    const char *cop = is_max ? ">" : "<";
    const char *fs[MAXARGS];
    const char *a;
    const char *b;
    const char *cur;
    double vals[MAXARGS];
    int isint = 1;
    int k, j;

    for (j = 0; j < n; j++)
        if (args[j].ty != TY_I)
            isint = 0;
    for (j = 0; j < n; j++)
        if (!literal_value(args[j].code, &vals[j]))
            break;
    if (j == n) {
        int best = 0;

        for (j = 1; j < n; j++)
            if (is_max ? vals[j] > vals[best] : vals[j] < vals[best])
                best = j;
        return mkval(as_flt(args[best]), TY_F);
    }
    for (j = 0; j < n; j++)
        fs[j] = isint ? args[j].code : as_flt(args[j]);
    for (j = 0; j < n; j++)
        if (has_call(fs[j]))
            break;
    if (n <= 4 && j == n) {
        cur = fs[0];
        for (j = 1; j < n; j++)
            cur = sfmt("((%s) %s (%s) ? (%s) : (%s))",
                       fs[j], cop, cur, fs[j], cur);
    } else {
        k = cv.mx_n[isint]++;
        if (cv.mode == M_SCAN) {
            int *own = cv.cur == NULL ? cv.mx_main : cv.cur->mx_pairs;

            if (k + 1 > own[isint])
                own[isint] = k + 1;
        }
        a = sfmt(isint ? "__mxia%d" : "__mxa%d", k);
        b = sfmt(isint ? "__mxib%d" : "__mxb%d", k);
        cur = sfmt("(%s = %s", a, fs[0]);
        for (j = 1; j < n; j++) {
            cur = sfmt("%s, %s = %s", cur, b, fs[j]);
            cur = sfmt("%s, %s = %s %s %s ? %s : %s",
                       cur, a, b, cop, a, b, a);
        }
        cur = sfmt("%s, %s)", cur, a);
    }
    if (isint)
        return mkval(sfmt("(MMFLOAT)(%s)", cur), TY_F);
    return mkval(cur, TY_F);
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
        /* FnInt is FloatToInt64(floor(x)), range-checked (a40112b): a
           bare cast of NaN or 1e30 is undefined.  floor's answer is
           whole, so mm_toint's rounding leaves it alone. */
        return mkval(sfmt("mm_toint(mm_int(%s))", f(0)), TY_I);
    }
    if (strcmp(up, "FIX") == 0) {
        if (args[0].ty == TY_I)
            return mkval(args[0].code, TY_I);
        return mkval(sfmt("mm_toint(mm_fix(%s))", f(0)), TY_I);   /* FnFix */
    }
    if (strcmp(up, "CINT") == 0)
        return mkval(sfmt("mm_toint(%s)", f(0)), TY_I);
    if (strcmp(up, "SGN") == 0)
        return mkval(sfmt("mm_sgn(%s)", f(0)), TY_I);
    {
        static const struct { const char *name; const char *chk;
                              const char *raw; } m[] = {
            /* SQR/LOG/ASIN/ACOS carry the firmware's domain checks so
               that ON ERROR can trap them; with no ON ERROR in the
               program the checks have no customer and the calls go
               straight to libm.  The rest have no checks and are
               always direct. */
            { "SQR", "mm_sqr", "sqrt" }, { "SIN", "sin", "sin" },
            { "COS", "cos", "cos" }, { "TAN", "tan", "tan" },
            { "ATN", "atan", "atan" }, { "LOG", "mm_log", "log" },
            { "EXP", "exp", "exp" }, { "ASIN", "mm_asin", "asin" },
            { "ACOS", "mm_acos", "acos" },
            { NULL, NULL, NULL }
        };
        int k;

        for (k = 0; m[k].name; k++)
            if (strcmp(up, m[k].name) == 0) {
                const char *cf = checks_on() ? m[k].chk : m[k].raw;

                /* OPTION ANGLE DEGREES: the argument goes in divided
                   (fun_sin/cos/tan), the answer comes out multiplied
                   (fun_atn/asin/acos).  Nothing else in this list
                   moves. */
                if (cv.opt_angle != NULL
                    && (strcmp(up, "SIN") == 0 || strcmp(up, "COS") == 0
                        || strcmp(up, "TAN") == 0))
                    return mkval(sfmt("%s((%s) / %s)", cf, f(0),
                                      cv.opt_angle), TY_F);
                if (cv.opt_angle != NULL
                    && (strcmp(up, "ATN") == 0 || strcmp(up, "ASIN") == 0
                        || strcmp(up, "ACOS") == 0))
                    return mkval(sfmt("(%s(%s) * %s)", cf, f(0),
                                      cv.opt_angle), TY_F);
                return mkval(sfmt("%s(%s)", cf, f(0)), TY_F);
            }
    }
    if (strcmp(up, "ATAN2") == 0) {
        const char *a0 = f(0);
        const char *a1 = f(1);

        if (cv.opt_angle != NULL)
            return mkval(sfmt("(atan2(%s, %s) * %s)", a0, a1,
                              cv.opt_angle), TY_F);
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
    if (strcmp(up, "MAX") == 0 || strcmp(up, "MIN") == 0)
        return max_min(strcmp(up, "MAX") == 0, args, nargs);
    if (strcmp(up, "BIT") == 0) {
        const char *a0 = n(0);
        const char *a1 = n(1);

        return mkval(sfmt("(((%s) >> (%s)) & 1LL)", a0, a1), TY_I);
    }
    if (strcmp(up, "FLAG") == 0) {
        /* FLAG(n) - one scratch bit.  The assigning form is a
           statement, so a FLAG that reaches here is a read. */
        cv.uses_misc = 1;
        return mkval(sfmt("mm_flag_get(%s)", n(0)), TY_I);
    }
    if (strcmp(up, "LEN") == 0)
        return mkval(sfmt("(MMINTEGER)mm_slen(%s)", s(0)), TY_I);
    if (strcmp(up, "ASC") == 0)
        return mkval(sfmt("mm_asc(%s)", s(0)), TY_I);
    if (strcmp(up, "BYTE") == 0) {
        const char *a0 = s(0);
        const char *a1 = n(1);

        cv.uses_misc = 1;
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
    if (strcmp(up, "TAB") == 0) {
        if (cv.pr_chan != NULL)
            return mkval(sfmt("mm_ftab(%s, %s)", cv.pr_chan, n(0)), TY_S);
        return mkval(sfmt("mm_tab(%s)", n(0)), TY_S);
    }
    if (strcmp(up, "TIMER") == 0)
        return mkval("mm_timer()", TY_F);
    if (strcmp(up, "DATE$") == 0) {
        cv.uses_datetime = 1;
        return mkval("mm_date_str()", TY_S);
    }
    if (strcmp(up, "TIME$") == 0) {
        cv.uses_datetime = 1;
        return mkval("mm_time_str()", TY_S);
    }
    if (strcmp(up, "CWD$") == 0)
        return mkval("mm_cwd()", TY_S);
    if (strcmp(up, "INKEY$") == 0)
        /* The key that has been pressed, or "" - MMBasic's INKEY$, and
           how a graphics program watches for one without stopping to
           wait.  Before this it was not a function at all: "Inkey$"
           became an ordinary string variable, always empty, so LOOP
           WHILE INKEY$="" was an exit that could never be taken and the
           program had to be interrupted. */
        return mkval("mm_inkey()", TY_S);
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

        if (args[0].ty == TY_F) {
            /* VAL and a mixed CHOICE carry the type they have when they
               run, and STR$ shows it (see the PRINT items) - with its
               other arguments too, StrFormat's integer path for an
               integer: STR$(VAL("2000000"), 10) is "   2000000" */
            int neg;
            const char *vl = val_of(args[0].code, &neg);
            const struct mixed *mc;

            if (vl != NULL) {
                if (nargs == 1 && !neg)
                    return mkval(sfmt("mm_vstr(%s)", vl), TY_S);
                return mkval(sfmt("mm_valfmt(%s, %d, %s, %s, %s)", vl,
                                  neg, m2, nn, pad), TY_S);
            }
            mc = mixed_choice_get(args[0].code);
            if (mc != NULL) {
                struct val va = mixed_a(mc), vb = mixed_b(mc);

                return mkval(sfmt("((%s) != 0 ? mm_str_%s(%s, %s, %s, %s)"
                                  " : mm_str_%s(%s, %s, %s, %s))",
                                  mixed_cond(mc),
                                  va.ty == TY_I ? "i" : "f", va.code,
                                  m2, nn, pad,
                                  vb.ty == TY_I ? "i" : "f", vb.code,
                                  m2, nn, pad), TY_S);
            }
        }
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

        cv.uses_misc = 1;
        return mkval(sfmt("mm_field(%s, %s, %s, %s)", a0, a1, delim, quote),
                     TY_S);
    }
    if (strcmp(up, "MM.SPISPEED") == 0) {
        /* the clock SPI OPEN actually got, which is rarely the one
           asked for - see mmb_spi.h */
        cv.uses_spi = 1;
        return mkval("mmspi_speed()", TY_I);
    }
    if (strcmp(up, "TEMPR") == 0) {
        /* TEMPR(pin [, timeout]) - the DS18B20's answer.  It SLEEPS
           while the conversion runs where MMBasic spins; see
           mmb_onewire.h. */
        const char *a0 = n(0);
        const char *a1 = nargs > 1 ? n(1) : "-1";

        cv.uses_gpio = 1;
        cv.uses_onewire = 1;
        return mkval(sfmt("mmow_tempr(%s, %s)", a0, a1), TY_F);
    }
    if (strcmp(up, "PULSIN") == 0) {
        /* Pulsin(pin, polarity [, t1 [, t2]]) - the width of the next
           pulse in microseconds, or -1 on any timeout.  A missing t2 is
           passed as -1 rather than by repeating t1, so a t1 that calls a
           FUNCTION is evaluated once, as the reference evaluates it.
           The measurement itself is the kernel's edge timestamps -
           PLAN-pulsin.md says why a busy-wait cannot do this here. */
        const char *a0 = n(0);
        const char *a1 = n(1);
        const char *a2 = nargs > 2 ? n(2) : "100000LL";
        const char *a3 = nargs > 3 ? n(3) : "-1LL";

        cv.uses_gpio = 1;
        cv.uses_pulsin = 1;
        return mkval(sfmt("mmg_pulsin(%s, %s, %s, %s)", a0, a1, a2, a3),
                     TY_I);
    }
    if (strcmp(up, "DISTANCE") == 0) {
        /* Distance(trig [, echo]) - centimetres, -1 no echo, -2 no
           acknowledgement.  One pin means a 3-pin device where the
           trigger and the echo are the same wire, and -1 is how that
           reaches the runtime. */
        const char *a0 = n(0);
        const char *a1 = nargs > 1 ? n(1) : "-1LL";

        cv.uses_gpio = 1;
        cv.uses_pulsin = 1;
        return mkval(sfmt("mmg_distance(%s, %s)", a0, a1), TY_F);
    }
    if (strcmp(up, "MM.I2C") == 0)
        return mkval("mm_i2c_stat()", TY_I);
    if (strcmp(up, "MM.ONEWIRE") == 0) {
        /* What the last ONEWIRE RESET saw - MMBasic's mmOWvalue, and a
           flat spelling there too. */
        cv.uses_gpio = 1;
        cv.uses_onewire = 1;
        return mkval("mmow_last()", TY_I);
    }
    if (strcmp(up, "POS") == 0) {
        /* POS - the column the next character will go in, 1 for the
           start of a line.  MMBasic's fun_pos returns MMCharPos, which
           the runtime has been tracking all along for TAB; this only
           gives it a name.  Inside PRINT #n it is that file's column
           (753ace5). */
        if (cv.pr_chan != NULL)
            return mkval(sfmt("(MMINTEGER)mm_fpos(%s)", cv.pr_chan), TY_I);
        return mkval("(MMINTEGER)mm_col()", TY_I);
    }
    if (strcmp(up, "MM.HRES") == 0)
        return mkval("mm_hres()", TY_I);
    if (strcmp(up, "MM.VRES") == 0)
        return mkval("mm_vres()", TY_I);
    if (strcmp(up, "MM.ERRNO") == 0)
        return mkval("mm_errno()", TY_I);
    if (strcmp(up, "MM.ERRMSG$") == 0)
        return mkval("mm_errmsg()", TY_S);
    if (strcmp(up, "MM.MESSAGE$") == 0) {
        /* the last UDP datagram - the WebMite's messagebuff.  A static
           in mmb_udp.h, not a scratch temp, so the reader costs
           nothing and survives mm_release. */
        cv.uses_udp = 1;
        return mkval("mm_udp_message()", TY_S);
    }
    if (strcmp(up, "MM.ADDRESS$") == 0) {
        cv.uses_udp = 1;
        return mkval("mm_udp_address()", TY_S);
    }
    if (strcmp(up, "MM.VER") == 0)
        return mkval("mm_ver()", TY_F);
    if (strcmp(up, "MM.DEVICE$") == 0)
        return mkval("mm_device()", TY_S);
    /* The flat spellings of four MM.INFO() answers.  MMBasic has both
       forms and programs use both - Pico-Vaders writes
       Mm.Info(FontHeight) where another writes MM.FONTHEIGHT - so they
       reach the same runtime call rather than one of them working. */
    if (strcmp(up, "MM.FONTHEIGHT") == 0)
        return mkval("mm_fontheight()", TY_I);
    if (strcmp(up, "MM.FONTWIDTH") == 0)
        return mkval("mm_fontwidth()", TY_I);
    if (strcmp(up, "MM.HPOS") == 0)
        return mkval("mm_hpos()", TY_I);
    if (strcmp(up, "MM.VPOS") == 0)
        return mkval("mm_vpos()", TY_I);
    if (strcmp(up, "MM.CMDLINE$") == 0) {
        /* the only thing that needs main's arguments, so main only takes
           them when a program asks */
        cv.uses_cmdline = 1;
        return mkval("mm_cmdline()", TY_S);
    }
    if (strcmp(up, "KEYDOWN") == 0) {
        /* KEYDOWN(n): which keys are HELD, which INKEY$ cannot say - a
           character stream has no way to express "up and fire
           together".  0 the count, 1..6 the codes with 1 the most
           recent, 7 the modifiers, 8 the locks, exactly MMBasic's
           fun_keydown. */
        return mkval(sfmt("mm_keydown(%s)", n(0)), TY_I);
    }
    if (strcmp(up, "PIXEL") == 0) {
        /* PIXEL(x, y) reads a pixel back AS RGB888 - the kernel
           primitive maps the mode's own colour numbering back out,
           so nothing here knows about depths or palettes. */
        const char *a0 = n(0);
        const char *a1 = n(1);

        return mkval(sfmt("mm_pixel_get(%s, %s)", a0, a1), TY_I);
    }
    if (strcmp(up, "PORT") == 0) {
        /* PORT(pin, nbits [, pin, nbits]...) - several pins read as one
           integer, all sampled at the same instant.  Pairs, so an odd
           count is a syntax error rather than a silently dropped
           argument; MMBasic checks (argc & 0b11) != 0b11. */
        const char *iv[MAXARGS];
        char g[512];
        int k;
        size_t len = 0;

        if (nargs % 2)
            cv_err("PORT takes pin, nbits pairs");
        for (k = 0; k < nargs; k++)
            iv[k] = as_int(args[k]);
        /* A comma sequence: the groups are written, then read.  C
           sequences the comma operator left to right, so the table is
           full before mmg_port_get looks at it. */
        g[0] = 0;
        for (k = 0; k < nargs / 2; k++) {
            len += (size_t)snprintf(g + len, sizeof(g) - len,
                                    "%smmg_port_group(%d, %s, %s)",
                                    k ? ", " : "", k, iv[k * 2],
                                    iv[k * 2 + 1]);
            if (len >= sizeof(g))
                cv_err("PORT argument list too long");
        }
        cv.uses_gpio = 1;
        cv.uses_port = 1;
        return mkval(sfmt("(%s, mmg_port_get(%d))", g, nargs / 2), TY_I);
    }
    if (strcmp(up, "PIN") == 0) {
        /* PIN(n) - a digital level, a raw ADC count, or a voltage,
           depending on what SETPIN made the pin.  The assigning form
           PIN(n) = v is a statement.

           FLOAT, always - see mmb2c.py's note.  MMBasic decides this
           at run time; generated C cannot, because nothing here knows
           what mode a pin will be in.  A double holds 0, 1 and every
           12-bit count exactly; an integer cannot hold 1.6523 volts. */
        cv.uses_gpio = 1;
        return mkval(sfmt("mmg_pin_get(%s)", n(0)), TY_F);
    }
    if (strcmp(up, "SPI") == 0) {
        /* SPI(x) - send one unit and return the one that came back.
           The command forms (OPEN, WRITE, READ, CLOSE) are statements;
           this is the function, so it is an integer. */
        cv.uses_spi = 1;
        return mkval(sfmt("mmspi_xfer1(%s)", n(0)), TY_I);
    }
    if (strcmp(up, "MAP") == 0) {
        /* MAP(n) - the colour entry n stands for by default, which is
           what a program must ask for to land on that entry.
           Unaffected by remapping, as MMBasic's fun_map is. */
        cv.uses_misc = 1;
        return mkval(sfmt("mm_map_get(%s)", n(0)), TY_I);
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
        if (a.ty == TY_S) {
            /* a COPY, as fun_ternary makes (a121e4e, issue #84): the bare
               ?: handed out the variable's own storage, and a SUB given
               CHOICE(1, b$, c$) changed b$ through it */
            cv.tmp_used = 1;
            return mkval(sfmt("mm_scopy((%s) != 0 ? (char *)(%s) : "
                              "(char *)(%s))", c.code, a.code, b.code), TY_S);
        }
        if (a.ty == TY_I && b.ty == TY_I)
            return mkval(sfmt("((%s) != 0 ? (%s) : (%s))",
                              c.code, a.code, b.code), TY_I);
        {
            const char *fa = as_flt(a);
            const char *fb = as_flt(b);
            char *code;

            if (a.ty == b.ty)
                return mkval(sfmt("((%s) != 0 ? (%s) : (%s))",
                                  c.code, fa, fb), TY_F);
            /* fun_ternary returns the chosen argument with its OWN type,
               decided as it runs.  A float covers both here; PRINT, STR$
               and an integer target look the parts up (mixed_choice) and
               take each branch in its type.  Its own shape of text, so a
               CHOICE of two floats can never be taken for one. */
            code = sfmt("(((%s) != 0) ? (%s) : (%s))", c.code, fa, fb);
            mixed_choice_put(code, c.code, a, b);
            return mkval(code, TY_F);
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
        return mkval(bound_of(sym, dim, has_dim), TY_I);
    }

    if (strcmp(up, "TRIM$") == 0) {
        struct val src;
        const char *mask = "\"\\001\" \" \"";
        const char *where = "\"\\001\" \"L\"";

        expect_op("(");
        src = expr();
        if (src.ty != TY_S)
            cv_err("TRIM$() needs a string");
        if (accept_op(",")) {
            /* an empty slot keeps the default mask, as fun_trim's
               `*argv[2]` test does: TRIM$(s$, , "R") */
            if (!is_op(",", 0)) {
                struct val m = expr();

                if (m.ty != TY_S)
                    cv_err("TRIM$() mask must be a string");
                mask = m.code;
            }
            if (accept_op(",")) {
                struct tok *t = peek(0);

                if (t != NULL && t->kind == T_ID
                    && (strcmp(t->up, "L") == 0 || strcmp(t->up, "R") == 0
                        || strcmp(t->up, "B") == 0)) {
                    where = sfmt("\"\\001\" \"%s\"", t->up);
                    cv.i += 1;
                } else {
                    struct val w = expr();

                    if (w.ty != TY_S)
                        cv_err("TRIM$() 'where' must be L, R or B");
                    where = w.code;
                }
            }
        }
        expect_op(")");
        cv.uses_misc = 1;
        return mkval(sfmt("mm_trim(%s, %s, %s)", src.code, mask, where),
                     TY_S);
    }

    if (strcmp(up, "DATETIME$") == 0 || strcmp(up, "DAY$") == 0
        || strcmp(up, "EPOCH") == 0) {
        const char *arg = NULL;

        cv.uses_datetime = 1;
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
        cv.uses_misc = 1;
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
        if (is_op(")", 0)) {
            /* getcsargs finds no argument at all: SyntaxError */
            cv.i++;
            return mkval("(mm_error(\"Invalid syntax\"), 0LL)", TY_I);
        }
        t = peek(0);
        if (one_arg()) {
            /* One argument is only ever a colour name to fun_rgb, matched
               by checkstring: the word, then anything that cannot go on
               with a name - RGB(RED + 0) and RGB(Red%) are red, and the
               rest is never evaluated.  Anything else is "Invalid colour:
               <the argument as written>" - raised when it runs, as
               there. */
            const char *msg = "Invalid colour: ";
            int depth = 0;
            int sfx;
            long val = t->kind == T_ID
                ? rgbname_get(upper(split_suffix(t->text, &sfx))) : -1;

            while (depth || !is_op(")", 0)) {
                struct tok *u = nxt();

                if (u->kind == T_OP && strcmp(u->text, "(") == 0)
                    depth++;
                else if (u->kind == T_OP && strcmp(u->text, ")") == 0)
                    depth--;
                msg = u->kind == T_STR ? sfmt("%s\"%s\"", msg, u->text)
                                       : sfmt("%s%s", msg, u->text);
            }
            expect_op(")");
            if (val >= 0)
                return mkval(sfmt("0x%06lXLL", (unsigned long)val), TY_I);
            return mkval(sfmt("(mm_error(\"%s\"), 0LL)", c_escape(msg)),
                         TY_I);
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
        {
            /* each is getint(..., 0, 255): literals in range fold, the
               rest are checked as they run */
            long long lr, lg, lb;

            if (int_literal(ri, &lr) && int_literal(gi, &lg)
                && int_literal(bi, &lb) && lr >= 0 && lr <= 255
                && lg >= 0 && lg <= 255 && lb >= 0 && lb <= 255)
                return mkval(sfmt("0x%06lXLL",
                                  (unsigned long)(lr << 16 | lg << 8 | lb)),
                             TY_I);
        }
        cv.uses_misc = 1;
        return mkval(sfmt("mm_rgb(%s, %s, %s)", ri, gi, bi), TY_I);
    }

    if (strcmp(up, "JSON$") == 0) {
        /* the streaming path-walker over a LONGSTRING document -
           mmb_json.h, fun_json's surface */
        struct flat ls;
        struct val p;

        expect_op("(");
        cv.uses_json = 1;
        ls = lsref();
        expect_op(",");
        p = expr();
        if (p.ty != TY_S)
            cv_err("JSON$ needs a string path");
        expect_op(")");
        return mkval(sfmt("mm_json(%s, %s, %s)",
                          ls.ptr, ls.cnt, p.code), TY_S);
    }

    if (strcmp(up, "LLEN") == 0 || strcmp(up, "LGETSTR$") == 0
        || strcmp(up, "LGETBYTE") == 0 || strcmp(up, "LINSTR") == 0
        || strcmp(up, "LCOMPARE") == 0 || strcmp(up, "LINPUT") == 0) {
        struct flat ls;
        struct val a;
        struct val b;

        expect_op("(");
        cv.uses_lstring = 1;
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
            return mkval(sfmt("mm_ls_getbyte(%s, %s, %s, %d)",
                              ls.ptr, ls.cnt, as_int(a), cv.opt_base), TY_I);
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

    if (strcmp(up, "PEEK") == 0) {
        /* PEEK(BYTE addr) and its wider relatives.  The width is a bare
           keyword, not a string and not a comma-separated argument,
           which is why this is parsed here - MMBasic's spelling. */
        struct tok *t;
        struct val a;
        const char *fn = NULL;
        int isfloat = 0;

        expect_op("(");
        t = nxt();
        if (t->kind == T_ID && strcmp(t->up, "VARADDR") == 0) {
            /* PEEK(VARADDR v) - where a variable lives.  This one needs
               the SYMBOL, not an address, which is why it is here and
               not in the header: the translator is the only thing that
               knows where a variable is.  A STRING gives the address of
               its LENGTH BYTE, because that is where an MMBasic string
               starts and ours have the same layout. */
            const char *r = varaddr();
            expect_op(")");
            return mkval(r, TY_I);
        }
        if (t->kind == T_ID && strcmp(t->up, "VAR") == 0) {
            /* PEEK(VAR v, n) - the byte n into a variable's storage,
               which is PEEK(BYTE (VARADDR v) + n): Exile reads its
               tables so, Peek(VAR tbl(), at + 1). */
            const char *r = varaddr();
            const char *n;

            expect_op(",");
            n = as_int(expr());
            expect_op(")");
            cv.uses_peek = 1;
            return mkval(sfmt("mmpk_byte((%s) + (%s))", r, n), TY_I);
        }
        if (t->kind == T_ID) {
            if (strcmp(t->up, "BYTE") == 0)         fn = "mmpk_byte";
            else if (strcmp(t->up, "SHORT") == 0)   fn = "mmpk_short";
            else if (strcmp(t->up, "WORD") == 0)    fn = "mmpk_word";
            else if (strcmp(t->up, "INTEGER") == 0) fn = "mmpk_integer";
            else if (strcmp(t->up, "FLOAT") == 0) {
                fn = "mmpk_float";
                isfloat = 1;
            }
        }
        if (fn == NULL)
            /* CFUNADDR is MMBasic's and is not here: it names an
               embedded blob a compiler has no equivalent for. */
            cv_err("PEEK(%s ...) is not supported; translated are "
                   "BYTE, SHORT, WORD, INTEGER, FLOAT, VAR and VARADDR",
                   t->text);
        a = expr();
        expect_op(")");
        cv.uses_peek = 1;
        return mkval(sfmt("%s(%s)", fn, as_int(a)), isfloat ? TY_F : TY_I);
    }

    if (strcmp(up, "SPRITE") == 0) {
        /* SPRITE(selector, ...) - Sprite.c fun_sprite, engine in
           mmb_sprite.h.  The letters become the reference's own t
           codes; V and D return floats (a bearing in radians and a
           centre distance), everything else integers.  SPRITE(B...)
           is the bounds-analysis machinery and is not translated. */
        static const struct { const char *nm; int code; } sels[] = {
            { "W", 1 }, { "H", 2 }, { "X", 3 }, { "Y", 4 },
            { "L", 5 }, { "C", 6 }, { "V", 7 }, { "T", 8 },
            { "E", 9 }, { "D", 10 }, { "A", 11 }, { "N", 12 },
            { "S", 13 }, { NULL, 0 }
        };
        const char *n;
        int sel = 0, si;

        cv.uses_sprite = 1;
        cv.uses_blit = 1;
        expect_op("(");
        if (is_kw("ST", 0)) {
            static const struct { const char *nm; int code; } props[] = {
                { "X", 1 }, { "Y", 2 }, { "W", 3 }, { "H", 4 },
                { "A", 5 }, { NULL, 0 }
            };
            int prop = 0;

            cv.i += 1;
            expect_op(",");
            if (is_kw("COLLISION", 0)) {
                cv.i += 1;
                expect_op(")");
                return mkval("mms_fun_st(1, 0, 0)", TY_I);
            }
            if (is_kw("OBJECT", 0)) {
                cv.i += 1;
                expect_op(")");
                return mkval("mms_fun_st(2, 0, 0)", TY_I);
            }
            accept_op("#");
            n = as_int(expr());
            expect_op(",");
            for (si = 0; props[si].nm; si++)
                if (is_kw(props[si].nm, 0)) {
                    cv.i += 1;
                    prop = props[si].code;
                    break;
                }
            if (prop == 0)
                cv_err("SPRITE(ST, n, ...) wants X, Y, W, H or A");
            expect_op(")");
            return mkval(sfmt("mms_fun_st(0, %s, %d)", n, prop), TY_I);
        }
        for (si = 0; sels[si].nm; si++)
            if (is_kw(sels[si].nm, 0)) {
                cv.i += 1;
                sel = sels[si].code;
                break;
            }
        if (sel == 0) {
            if (is_kw("B", 0))
                cv_err("SPRITE(B ...) is not translated");
            cv_err("SPRITE() wants a selector letter");
        }
        if (sel == 13) {
            /* SPRITE(S) - the sprite that triggered the last collision
               interrupt.  An argument is accepted and ignored, because
               the reference accepts and ignores one: fun_sprite parses
               up to five arguments for every selector, and the t==13
               arm is "iret = sprite_which_collided" without ever
               looking at argv[2] (Sprite.c:2462).  Programs are written
               both ways - brownian.bas says SPRITE(S, i) - and refusing
               the second argument rejected a line MMBasic runs. */
            if (accept_op(",")) {
                accept_op("#");
                expr();
            }
            expect_op(")");
            return mkval("mms_fun(13, 0, 0, 1)", TY_I);
        }
        if (sel == 12) {
            if (accept_op(",")) {
                const char *l = as_int(expr());

                expect_op(")");
                return mkval(sfmt("mms_fun(12, %s, 0, 2)", l), TY_I);
            }
            expect_op(")");
            return mkval("mms_fun(12, 0, 0, 1)", TY_I);
        }
        expect_op(",");
        accept_op("#");
        n = as_int(expr());
        if (sel == 7 || sel == 10) {
            const char *m;

            expect_op(",");
            accept_op("#");
            m = as_int(expr());
            expect_op(")");
            return mkval(sfmt("mms_fun_f(%d, %s, %s)", sel, n, m), TY_F);
        }
        if (accept_op(",")) {
            const char *i3 = as_int(expr());

            expect_op(")");
            return mkval(sfmt("mms_fun(%d, %s, %s, 2)", sel, n, i3), TY_I);
        }
        expect_op(")");
        return mkval(sfmt("mms_fun(%d, %s, 0, 1)", sel, n), TY_I);
    }
    if (strcmp(up, "TILEMAP") == 0) {
        /* TILEMAP(selector ...) - TileMap.c fun_tilemap, engine in
           mmb_tilemap.h.  The selector is a bare word and the
           arguments follow it WITHOUT a comma, as the reference parses
           them (checkstring, then getcsargs on the rest):
           TILEMAP(TILE 1, x, y), TILEMAP(SPRITE X 1).  Every answer is
           an integer. */
        static const struct { const char *nm; int code; } sels[] = {
            { "VIEWX", 1 }, { "VIEWY", 2 }, { "COLS", 3 }, { "ROWS", 4 },
            { NULL, 0 }
        };
        static const struct { const char *nm; int code; } ssels[] = {
            { "X", 1 }, { "Y", 2 }, { "TILE", 3 }, { "W", 4 }, { "H", 5 },
            { NULL, 0 }
        };
        const char *a[5];
        const char *n;
        int si;

        cv.uses_tilemap = 1;
        cv.uses_blit = 1;
        cv.uses_flash = 1;
        cv.uses_data = 1;
        expect_op("(");
        if (is_kw("TILE", 0)) {
            cv.i += 1;
            int_args(a, 3);
            expect_op(")");
            return mkval(sfmt("mmt_fn_tile(%s, %s, %s)", a[0], a[1], a[2]),
                         TY_I);
        }
        if (is_kw("COLLISION", 0)) {
            const char *mask = "0LL";

            cv.i += 1;
            int_args(a, 5);
            if (accept_op(","))
                mask = as_int(expr());
            expect_op(")");
            return mkval(sfmt("mmt_fn_coll(%s, %s, %s, %s, %s, %s)",
                              a[0], a[1], a[2], a[3], a[4], mask), TY_I);
        }
        if (is_kw("ATTR", 0)) {
            cv.i += 1;
            int_args(a, 2);
            expect_op(")");
            return mkval(sfmt("mmt_fn_attr(%s, %s)", a[0], a[1]), TY_I);
        }
        for (si = 0; sels[si].nm; si++)
            if (is_kw(sels[si].nm, 0)) {
                cv.i += 1;
                n = as_int(expr());
                expect_op(")");
                return mkval(sfmt("mmt_fn(%d, %s)", sels[si].code, n),
                             TY_I);
            }
        if (is_kw("SPRITE", 0)) {
            cv.i += 1;
            if (is_kw("HIT", 0)) {
                cv.i += 1;
                int_args(a, 2);
                expect_op(")");
                return mkval(sfmt("mmt_fn_hit(%s, %s)", a[0], a[1]), TY_I);
            }
            for (si = 0; ssels[si].nm; si++)
                if (is_kw(ssels[si].nm, 0)) {
                    cv.i += 1;
                    n = as_int(expr());
                    expect_op(")");
                    return mkval(sfmt("mmt_fn_sprite(%d, %s)",
                                      ssels[si].code, n), TY_I);
                }
            cv_err("TILEMAP(SPRITE ...) wants X, Y, TILE, W, H or HIT");
        }
        cv_err("TILEMAP() wants TILE, COLLISION, ATTR, VIEWX, VIEWY, "
               "COLS, ROWS or SPRITE");
    }
    /* MMBasic overlays MM.INFO and MM.INFO$ onto ONE function
       (fun_info), which decides the type from the sub-keyword rather
       than from the '$'.  So both spellings land here and the tables
       below say what each answer is. */
    if (strcmp(up, "MM.INFO") == 0 || strcmp(up, "MM.INFO$") == 0) {
        /* The sub-keywords this machine can answer.  MMBasic's own list
           is dozens long and nearly all of it is about hardware, a
           flash program store or a network that is not here; what is
           below is what a program running on a PC3 can use an answer
           to.  Two-word names are matched first, so EXISTS DIR is never
           read as EXISTS. */
        static const struct { const char *kw, *call; int ty; } plain[] = {
            { "FLAGS",      "mm_flags_get()", TY_I },
            { "FONTHEIGHT", "mm_fontheight()", TY_I },
            { "FONTWIDTH",  "mm_fontwidth()", TY_I },
            { "HPOS",       "mm_hpos()", TY_I },
            { "VPOS",       "mm_vpos()", TY_I },
            { "DEVICE",     "mm_device()", TY_S },
            { "PLATFORM",   "mm_platform()", TY_S },
            { "PATH",       "mm_path()", TY_S },
            { "CURRENT",    "mm_current()", TY_S },
            { "DRIVE",      "mm_drive()", TY_S },
            { "VERSION",    "mm_ver()", TY_F },
            { "ERRNO",      "mm_errno()", TY_I },
            { "ERRMSG",     "mm_errmsg()", TY_S },
            /* the reference is time_us_64()/1000000.0 as a FLOAT
             * (MM_Misc.c fun_info UPTIME); mm_us() is the same 64-bit
             * microsecond clock - the kernel's on the board, so
             * seconds since boot */
            { "UPTIME",     "((MMFLOAT)mm_us() / 1000000.0)", TY_F },
            /* the current drive's capacity in bytes, via statvfs */
            { "DISK SIZE",  "mm_disksize()", TY_I },
            /* and its free bytes, the same way */
            { "FREE SPACE", "mm_freespace()", TY_I },
            /* the PicoMite's PSRAM in bytes: 8 MB on a PC3, 0 with none */
            { "PSRAM SIZE", "mm_psram_size()", TY_I },
            /* the heap's free bytes, as FreeSpaceOnHeap counts them:
               what arrays and strings have left (Elite sizes its 3D
               objects by the difference across a Draw3D CREATE) */
            { "HEAP",       "mm_heapfree()", TY_I },
            /* the mode as a VGA or HDMI PicoMite names it, "MODE1"
               and on, or "" with no display: INSTR(..., "MODE") tells
               VGA or HDMI from an LCD panel */
            { "LCDPANEL",   "mm_lcdpanel()", TY_S },
            /* "Not set", "I2C" or "I2C2" on the firmware.  The PC3's
               clock and QWIIC socket are on I2C, GP20/GP21, which the
               kernel runs - the firmware's answer for OPTION SYSTEM I2C
               on those pins */
            { "SYSTEM I2C", "\"\\003\" \"I2C\"", TY_S },
            { NULL, NULL, 0 }
        };
        static const struct { const char *kw, *call; } witharg[] = {
            { "PINNO",       "mm_pinno(%s)" },
            { "FILESIZE",    "mm_filesize(%s)" },
            { "EXISTS FILE", "mm_exists_file(%s)" },
            { "EXISTS DIR",  "mm_exists_dir(%s)" },
            { NULL, NULL }
        };
        struct tok *t, *nx;
        struct val a;
        char two[64];
        int i, words;

        expect_op("(");
        t = nxt();
        if (t->kind != T_ID)
            cv_err("MM.INFO wants a keyword, not %s", t->text);
        two[0] = 0;
        nx = peek(0);
        if (nx != NULL && nx->kind == T_ID
            && strlen(t->up) + strlen(nx->up) + 2 <= sizeof(two)) {
            strcpy(two, t->up);
            strcat(two, " ");
            strcat(two, nx->up);
        }

        /* OPTION BASE is answered HERE, at translation time: it is a
           compile-time setting for this translator, so the value is
           already known and a run-time call could only look it up
           again.  It also unblocks the shape the Game*Mite ctrl library
           opens with, Dim ctrl.key_map%(31 + Mm.Info(Option Base)). */
        if (strcmp(two, "OPTION BASE") == 0) {
            (void)nxt();
            expect_op(")");
            return mkval(sfmt("%dLL", cv.opt_base), TY_I);
        }
        /* The network answers.  IP ADDRESS asks the kernel
           (NETIOC_STATUS) and is "0.0.0.0" when there is no join - the
           WebMite's own idle answer, which retic.bas polls for at
           startup.  MAX CONNECTIONS is the slot count, 8 on both. */
        if (strcmp(two, "IP ADDRESS") == 0) {
            (void)nxt();
            expect_op(")");
            cv.uses_net = 1;
            return mkval("mmn_ipaddr()", TY_S);
        }
        if (strcmp(two, "MAX CONNECTIONS") == 0) {
            (void)nxt();
            expect_op(")");
            return mkval("8LL", TY_I);
        }
        if (strcmp(t->up, "PIN") == 0) {
            /* MM.INFO(PIN n): the pin's function as the firmware names
               it - mmg_pininfo, beside SETPIN in mmb_gpio.h */
            a = expr();
            expect_op(")");
            cv.uses_gpio = 1;
            return mkval(sfmt("mmg_pininfo(%s)", as_int(a)), TY_S);
        }
        for (words = 2; words >= 1; words--) {
            const char *key = (words == 2) ? two : t->up;
            if (key[0] == 0)
                continue;
            for (i = 0; witharg[i].kw != NULL; i++) {
                if (strcmp(key, witharg[i].kw) != 0)
                    continue;
                if (words == 2)
                    (void)nxt();
                a = expr();
                if (strcmp(key, "PINNO") == 0 && a.ty == TY_I) {
                    /* MM.INFO(PINNO GP1), unquoted.  MMBasic takes both:
                       fun_info's PINNO checks the raw text for "GPnn"
                       before evaluating it, so a bare pin name is legal
                       there as well as a string.  Here a bare GP1 has
                       already become the integer 1 - the name IS the
                       number on this machine - so the answer is the
                       value, and only the string form needs parsing. */
                    expect_op(")");
                    return mkval(a.code, TY_I);
                }
                if (a.ty != TY_S)
                    cv_err("MM.INFO(%s ...) wants a string", key);
                expect_op(")");
                return mkval(sfmt(witharg[i].call, a.code), TY_I);
            }
            for (i = 0; plain[i].kw != NULL; i++) {
                if (strcmp(key, plain[i].kw) != 0)
                    continue;
                if (words == 2)
                    (void)nxt();
                /* Both PATH and CURRENT are argv[0], which main only
                   receives when a program asks for something needing
                   it - the same flag MM.CMDLINE$ raises. */
                if (strcmp(key, "PATH") == 0 || strcmp(key, "CURRENT") == 0)
                    cv.uses_cmdline = 1;
                /* mm_flags_get lives in mmb_misc.h with the rest of
                   the FLAG family. */
                if (strcmp(key, "FLAGS") == 0)
                    cv.uses_misc = 1;
                expect_op(")");
                return mkval(plain[i].call, plain[i].ty);
            }
        }
        if (t->kind == T_ID && strcmp(t->up, "FLASH") == 0) {
            /* MM.INFO(FLASH ADDRESS n) - the slot's base address,
               which is how a program hands slot data to BLIT MEMORY.
               The pseudo slot allocates on this very reference
               (mmb_flash.h), so asking for the address is enough. */
            t = nxt();
            if (t->kind != T_ID || strcmp(t->up, "ADDRESS") != 0)
                cv_err("MM.INFO(FLASH %s ...) is not supported; "
                       "translated is FLASH ADDRESS n", t->text);
            a = expr();
            expect_op(")");
            cv.uses_flash = 1;
            return mkval(sfmt("(MMINTEGER)(long)mmf_addr(%s)", as_int(a)),
                         TY_I);
        }
        if (t->kind != T_ID || strcmp(t->up, "FONT") != 0)
            cv_err("MM.INFO(%s ...) is not supported; translated are "
                   "DEVICE, PLATFORM, PATH, CURRENT, DRIVE, VERSION, "
                   "ERRNO, ERRMSG, FLAGS, FONTHEIGHT, FONTWIDTH, HPOS, "
                   "VPOS, UPTIME, DISK SIZE, FREE SPACE, PSRAM SIZE, HEAP, "
                   "LCDPANEL, SYSTEM I2C, IP ADDRESS, "
                   "MAX CONNECTIONS, OPTION BASE, PIN n, PINNO, FILESIZE, "
                   "EXISTS FILE, EXISTS DIR, FONT ADDRESS n and "
                   "FLASH ADDRESS n", t->text);
        t = nxt();
        if (t->kind != T_ID || strcmp(t->up, "ADDRESS") != 0)
            cv_err("MM.INFO(FONT %s ...) is not supported; translated "
                   "is FONT ADDRESS n", t->text);
        a = expr();
        expect_op(")");
        return mkval(sfmt("mm_fontaddr(%s)", as_int(a)), TY_I);
    }

    if (strcmp(up, "MATH") == 0) {
        struct tok *t;

        expect_op("(");
        /* mmb2c.py's CRCWIDTH and do_math_crc, in the same order */
        t = nxt();
        if (t->kind == T_ID && strcmp(t->up, "BASE64") == 0) {
            /* MATH(BASE64 ENCODE in$, out$): returns the length,
             * writes the string into the second argument - fun_math's
             * own odd call shape, and how retic.bas writes it (the
             * out argument being the enclosing Function's result
             * variable).  Strings only; the reference also takes
             * arrays, which nothing has needed yet - refused, not
             * diverged. */
            struct tok *w = nxt();
            struct val a;
            struct tok *t2;
            struct val tgt;
            int cap;
            const char *fn;

            if (w->kind != T_ID || (strcmp(w->up, "ENCODE") != 0
                                    && strcmp(w->up, "DECODE") != 0))
                cv_err("MATH(BASE64 ...) wants ENCODE or DECODE");
            a = expr();
            if (a.ty != TY_S)
                cv_err("MATH(BASE64 %s) needs a string", w->up);
            expect_op(",");
            t2 = peek(0);
            if (t2 == NULL || t2->kind != T_ID)
                cv_err("MATH(BASE64 %s) output must be a string"
                       " variable", w->up);
            tgt = input_target(&cap);
            if (tgt.ty != TY_S)
                cv_err("MATH(BASE64 %s) output must be a string"
                       " variable", w->up);
            expect_op(")");
            cv.uses_math = 1;
            fn = strcmp(w->up, "ENCODE") == 0 ? "mmg_b64_enc"
                : "mmg_b64_dec";
            return mkval(sfmt("%s(%s, %s, %d)", fn, a.code, tgt.code,
                              cap == 0 ? 255 : cap), TY_I);
        }
        if (t->kind == T_ID && crc_width(t->up) != 0)
            return do_math_crc(t->up);
        if (t->kind == T_ID && strcmp(t->up, "RAND") == 0) {
            /* MATH(RAND) - the Mersenne Twister, the pair to MATH
               RANDOMIZE.  Takes no argument.  NOT RND, which is rand()
               reseeded from the clock and is not seedable. */
            expect_op(")");
            cv.uses_mt = 1;
            return mkval("mmg_mt_rand()", TY_F);
        }
        if (t->kind == T_ID && strcmp(t->up, "CROSSING") == 0) {
            /* MATH(CROSSING a() [, level] [, direction] [, confirm])
             *
             * A one-dimensional number array; the three tails are
             * optional and may be written empty, as the CRC family's
             * are.  What comes back is a subscript, the offset plus
             * OPTION BASE (ec27921). */
            struct sym *a = arrayref(1);
            struct flat fl;
            const char *args[3];
            int k;

            if (a->ty == TY_S)
                cv_err("Argument 1 must be a 1D numerical array");
            args[0] = "0.0";
            args[1] = "1";
            args[2] = "1";
            for (k = 0; k < 3; k++) {
                struct val v;

                if (!accept_op(","))
                    break;
                if (is_op(",", 0) || is_op(")", 0))
                    continue;       /* an empty slot keeps the default */
                v = expr();
                args[k] = (k == 0) ? as_flt(v) : as_int(v);
            }
            expect_op(")");
            fl = array_line(a);
            cv.uses_math = 1;
            return mkval(sfmt("mmg_crossing_%s(%s, %s, %s, %s, %s, %d)",
                              a->ty == TY_I ? "i" : "f", fl.ptr, fl.cnt,
                              args[0], args[1], args[2], cv.opt_base),
                         TY_I);
        }
        if (t->kind == T_ID && strcmp(t->up, "M_DETERMINANT") == 0) {
            /* MATH(M_DETERMINANT a())  - a square 2-D float array */
            struct sym *a = arrayref(1);
            struct plane p;

            if (a->ty != TY_F)
                cv_err("Argument 1 must be a floating point array");
            expect_op(")");
            p = array_plane(a);
            cv.uses_math = 1;
            return mkval(sfmt("mmg_mdet(%s, %s, %s)", p.ptr, p.nc, p.nr),
                         TY_F);
        }
        if (t->kind == T_ID && (strcmp(t->up, "MAGNITUDE") == 0
                                || strcmp(t->up, "DOTPRODUCT") == 0)) {
            /* MATH(MAGNITUDE a())        sqrt of the sum of squares
             * MATH(DOTPRODUCT a(), b())  sum of the products
             *
             * Float arrays both, which is parsefloatarray's own
             * restriction.  MAGNITUDE takes ANY rank (dimension count 0
             * in the reference) and reads the array flat; DOTPRODUCT is
             * one-dimensional, so it goes through array_line and counts
             * what a program can reach. */
            int mag = strcmp(t->up, "MAGNITUDE") == 0;
            struct sym *a = arrayref(1);
            struct sym *b;
            struct flat fa, fb;

            if (a->ty != TY_F)
                cv_err("Argument 1 must be a floating point array");
            cv.uses_math = 1;
            if (mag) {
                expect_op(")");
                fa = array_flat(a);
                return mkval(sfmt("mmg_magnitude(%s, %s)",
                                  fa.ptr, fa.cnt), TY_F);
            }
            expect_op(",");
            b = arrayref(1);
            if (b->ty != TY_F)
                cv_err("Argument 2 must be a floating point array");
            expect_op(")");
            fa = array_line(a);
            fb = array_line(b);
            return mkval(sfmt("mmg_dot(%s, %s, %s, %s)",
                              fa.ptr, fa.cnt, fb.ptr, fb.cnt), TY_F);
        }
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
            cv.uses_array = 1;
            if (strcmp(name, "MAX") == 0 || strcmp(name, "MIN") == 0)
                return mkval(sfmt("mm_st_%s_%s(%s, %s, %s, %d)",
                                  fn, sfx, fl.ptr, fl.cnt, idx,
                                  cv.opt_base), TY_F);
            return mkval(sfmt("mm_st_%s_%s(%s, %s)",
                              fn, sfx, fl.ptr, fl.cnt), TY_F);
        }
        if (t->kind != T_ID || mathfunc_get(t->up) == 0)
            /* sorted(MATHFUNCS + MATHARRAY + CRCWIDTH + the hand
             * branches) - keep in step with mmb2c.py and mmbc_tab.c */
            cv_err("MATH(%s ...) is not supported; translated are %s",
                   t->text,
                   "ATAN3, BASE64 DECODE, BASE64 ENCODE, COSH, CRC12, "
                   "CRC16, CRC32, CRC8, CROSSING, DOTPRODUCT, "
                   "LOG10, M_DETERMINANT, MAGNITUDE, MAX, MEAN, MEDIAN, "
                   "MIN, RAND, SD, SINH, SUM, TANH");
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

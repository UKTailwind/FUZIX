/* mmbc_lex.c - tokenize() and the small string helpers.
 *
 * Mirrors mmb2c.py from `def is_alpha` to `def clabel` - same branch
 * order, same messages.  Divergences from the Python are marked. */

#include "mmbc.h"

/*
 * Uppercase hex for a 64-bit value, into a caller's char[17].
 *
 * printf cannot be asked: the Fuzix libc's vsnprintf - which is what
 * sfmt uses, and mmbc runs on the board - has no long-long conversion,
 * and "%llX" produced "0" for every value.  Nothing said so; the
 * translated program simply had zeros where its &H constants should
 * have been.  Rendering the digits here is the same bargain
 * bc_strtoll makes for parsing them.
 */
static const char *hex64(unsigned long long v, char *buf)
{
    char *p = buf + 16;

    *p = '\0';
    if (v == 0)
        *--p = '0';
    else
        while (v != 0) {
            *--p = "0123456789ABCDEF"[(int)(v & 15)];
            v >>= 4;
        }
    return p;
}

const char *ctype_of(int ty)
{
    switch (ty) {
    case TY_F: return "MMFLOAT";
    case TY_I: return "MMINTEGER";
    case TY_S: return "char";
    }
    return "?";
}

const char *tyname_of(int ty)
{
    switch (ty) {
    case TY_F: return "FLOAT";
    case TY_I: return "INTEGER";
    case TY_S: return "STRING";
    }
    return "?";
}

int is_alpha(int c)
{
    return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '_';
}

int is_digit_c(int c)
{
    return '0' <= c && c <= '9';
}

int is_idchar(int c)
{
    return is_alpha(c) || is_digit_c(c) || c == '.';
}

int is_hexd(int c)
{
    return is_digit_c(c) || ('a' <= c && c <= 'f') || ('A' <= c && c <= 'F');
}

char *upper(const char *s)
{
    char *p = sstr(s);
    char *q;
    for (q = p; *q; q++)
        if ('a' <= *q && *q <= 'z')
            *q += 'A' - 'a';
    return p;
}

char *lower(const char *s)
{
    char *p = sstr(s);
    char *q;
    for (q = p; *q; q++)
        if ('A' <= *q && *q <= 'Z')
            *q += 'a' - 'A';
    return p;
}

/* Make text safe to sit inside a C comment. */
/* Is this emitted operand a number that cannot be zero?
 *
 * Only a plain literal counts - the text as it will appear in the C.  A
 * divisor like 180.0 or 86400.0 needs no divide-by-zero test, and on the
 * board that test is a call across the VM boundary, so knowing the
 * answer here is worth the few lines. */
int nonzero_literal(const char *code)
{
    const char *t = code;
    const char *e;
    int seen = 0;

    while (*t == ' ' || *t == '\t')
        t++;
    e = t + strlen(t);
    while (e > t && (e[-1] == 'L' || e[-1] == 'l'))
        e--;
    if (e == t)
        return 0;
    if (!(is_digit_c((unsigned char)*t) || (*t == '-' && e - t > 1)))
        return 0;
    for (; t < e; t++) {
        if (!(is_digit_c((unsigned char)*t) || strchr(".eE+-", *t) != NULL))
            return 0;
        if (is_digit_c((unsigned char)*t) && *t != '0')
            seen = 1;
    }
    return seen;              /* all zeros (0, 0.0, 0e0) is not "nonzero" */
}

/* A plain decimal integer literal, written as a float.
 *
 * "3600LL" becomes "3600.0" - the same double either way, since up to 15
 * digits every integer is exact - but nonzero_literal can read it, so
 * dividing by it needs no zero test.  Hex (from &H) keeps its cast,
 * "0x10.0" being no number, and so does a leading zero, which C reads as
 * octal.  NULL for anything that is not simply digits. */
const char *float_form_of_int_literal(const char *code)
{
    const char *t = code;
    const char *e;
    char buf[24];
    int n, i;

    while (*t == ' ' || *t == '\t')
        t++;
    e = t + strlen(t);
    while (e > t && (e[-1] == ' ' || e[-1] == '\t'))
        e--;
    while (e > t && (e[-1] == 'L' || e[-1] == 'l'))
        e--;
    n = (int)(e - t);
    if (n == 0 || n > 15 || (n > 1 && *t == '0'))
        return NULL;
    for (i = 0; i < n; i++)
        if (!is_digit_c((unsigned char)t[i]))
            return NULL;
    memcpy(buf, t, (size_t)n);
    buf[n] = '.';
    buf[n + 1] = '0';
    buf[n + 2] = '\0';
    return sfmt("%s", buf);
}

/* True when an emitted expression is already 0-or-1: a single
 * comparison at the top of its tree.  Only then may a condition skip
 * the "(...) != 0" wrapper; everything else keeps it, including the
 * AND/OR combinations (bitwise on integers in MMBasic) and bare
 * numbers.  Textual, like nonzero_literal: a comparison operator at
 * parenthesis depth 1 of the emitted form is the top of the tree,
 * string literals are skipped, "->" and shifts are not comparisons,
 * and a '?' at depth 1 is a ternary whose value needs the test. */
int boolean_expr(const char *code)
{
    int n = (int)strlen(code);
    int depth = 0;
    int seen = 0;
    int i;

    if (n == 0 || code[0] != '(')
        return 0;
    for (i = 0; i < n; i++) {
        char ch = code[i];

        if (ch == '"') {
            i++;
            while (i < n && code[i] != '"') {
                if (code[i] == '\\')
                    i++;
                i++;
            }
        } else if (ch == '(') {
            depth++;
        } else if (ch == ')') {
            depth--;
        } else if (depth == 1) {
            if (ch == '?')
                return 0;
            if (ch == '<' || ch == '>') {
                char nxt = i + 1 < n ? code[i + 1] : 0;

                if (nxt == ch) {
                    i++;
                } else if (ch == '>' && i > 0 && code[i - 1] == '-') {
                    ;
                } else {
                    seen = 1;
                    if (nxt == '=')
                        i++;
                }
            } else if ((ch == '=' || ch == '!') && i + 1 < n
                       && code[i + 1] == '=') {
                seen = 1;
                i++;
            }
        }
    }
    return seen && depth == 0;
}

char *cblock_safe(const char *text)
{
    size_t n = strlen(text);
    char *out = salloc(n + 1);
    size_t i, j = 0;

    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '*' && text[i + 1] == '/') {
            /* the Python replaces the pair with "* /" */
            out[j++] = '*';
            c = ' ';
            /* leave the '/' to the ordinary path next iteration */
        }
        out[j++] = (c >= 32 && c < 127) ? (char)c : ' ';
    }
    out[j] = 0;
    return out;
}

/* Python repr() of a one-character string, for the tokenizer's
 * "unexpected character %r" message.  Exact for ASCII input. */
static char *char_repr(int c)
{
    if (c == '\'')
        return sstr("\"'\"");
    if (c == '\\')
        return sstr("'\\\\'");
    if (c >= 32 && c < 127)
        return sfmt("'%c'", c);
    return sfmt("'\\x%02x'", c & 0xFF);
}

static const char *ops2[] = { "<=", ">=", "<>", "=<", "=>", "><",
                              "<<", ">>", NULL };
/* '.' is an operator ONLY when it survives identifier scanning - dots
 * inside a name are eaten greedily by is_idchar, so a '.' token can
 * only arise after ')' and the like: the arr(i).member form. */
static const char ops1[] = "+-*/\\^=<>(),;:?@#.";

static void addtok(struct tok *out, int *nt, int lineno,
                   int kind, const char *text, const char *up)
{
    if (*nt >= MAXTOKS)
        mm_error("line %d: too many tokens", lineno);
    out[*nt].kind = kind;
    out[*nt].text = text;
    out[*nt].up = up;
    (*nt)++;
}

/* Turn one source line into tokens; returns the count.  Resets the
 * scratch pool: the previous line's tokens and strings die here. */
int tokenize(const char *line, int lineno, struct tok *out)
{
    scratch_reset();
    return tokenize_frag(line, lineno, out);
}

/* The same lexer WITHOUT the scratch reset: a page expression is
   tokenized mid-statement (do_web_page), and resetting would free the
   texts the suspended line's tokens still point at.  The pool grows by
   the fragments' texts for the length of that one line, which is
   bounded by the page's expression count. */
int tokenize_frag(const char *line, int lineno, struct tok *out)
{
    int nt = 0;
    int i = 0;
    int n;

    n = (int)strlen(line);
    while (i < n) {
        char c = line[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            i++;
            continue;
        }
        if (c == '\x1a')                /* DOS end of file marker */
            break;
        if (c == '\'')                  /* comment to end of line */
            break;
        if (is_alpha(c)) {
            int j = i;
            char *word;
            char *up;
            while (j < n && is_idchar(line[j]))
                j++;
            if (j < n && (line[j] == '$' || line[j] == '%'
                          || line[j] == '!'))
                j++;
            word = salloc((size_t)(j - i) + 1);
            memcpy(word, line + i, (size_t)(j - i));
            word[j - i] = 0;
            up = upper(word);
            if (strcmp(up, "REM") == 0)  /* comment */
                break;
            addtok(out, &nt, lineno, T_ID, word, up);
            i = j;
            continue;
        }
        if (is_digit_c(c)
            || (c == '.' && i + 1 < n && is_digit_c(line[i + 1]))) {
            int j = i;
            int isf = 0;
            char *txt;
            while (j < n && is_digit_c(line[j]))
                j++;
            if (j < n && line[j] == '.') {
                isf = 1;
                j++;
                while (j < n && is_digit_c(line[j]))
                    j++;
            }
            if (j < n && (line[j] == 'e' || line[j] == 'E')) {
                int k = j + 1;
                if (k < n && (line[k] == '+' || line[k] == '-'))
                    k++;
                if (k < n && is_digit_c(line[k])) {
                    isf = 1;
                    j = k;
                    while (j < n && is_digit_c(line[j]))
                        j++;
                }
            }
            txt = salloc((size_t)(j - i) + 1);
            memcpy(txt, line + i, (size_t)(j - i));
            txt[j - i] = 0;
            addtok(out, &nt, lineno, T_NUM, txt, isf ? "F" : "I");
            i = j;
            continue;
        }
        if (c == '&') {                 /* &H &O &B -> integer */
            int j = i + 1;
            if (j < n && (line[j] == 'h' || line[j] == 'H'
                          || line[j] == 'o' || line[j] == 'O'
                          || line[j] == 'b' || line[j] == 'B')) {
                int base = 16;
                int k;
                unsigned long long val = 0;
                if (line[j] == 'o' || line[j] == 'O')
                    base = 8;
                else if (line[j] == 'b' || line[j] == 'B')
                    base = 2;
                j++;
                k = j;
                while (k < n && is_hexd(line[k]))
                    k++;
                if (k == j)
                    mm_error("line %d: bad &-constant", lineno);
                for (; j < k; j++) {
                    int d = line[j];
                    d = is_digit_c(d) ? d - '0'
                        : (d >= 'a' ? d - 'a' + 10 : d - 'A' + 10);
                    /* a digit outside the base tracebacks the Python
                     * (int() ValueError); we error instead */
                    if (d >= base)
                        mm_error("line %d: bad &-constant", lineno);
                    val = val * (unsigned)base + (unsigned)d;
                }
                /* unsigned 64-bit; hex so >2^63 cannot overflow.
                 *
                 * NOT sfmt("%llX"): sfmt goes through the Fuzix libc's
                 * vsnprintf, which has no long-long conversion and wrote
                 * "0" whatever the value was.  On the development
                 * machine glibc got it right, so this only ever went
                 * wrong for a program translated ON THE BOARD - where
                 * every &H, &O and &B constant silently became zero.
                 * It cost a day: an I2C2 write to &H77 went out
                 * addressed to 0 and the device did not answer, which
                 * looks exactly like a wiring fault.  hex64 renders the
                 * digits itself, the way bc_strtoll parses them itself
                 * for the same reason. */
                {
                    char hb[17];

                    addtok(out, &nt, lineno, T_NUM,
                           sfmt("((MMINTEGER)0x%sULL)", hex64(val, hb)),
                           "H");
                }
                i = k;
                continue;
            }
            mm_error("line %d: bad & constant", lineno);
        }
        if (c == '"') {
            int j = i + 1;
            char *buf;
            while (j < n && line[j] != '"')
                j++;
            if (j >= n)
                mm_error("line %d: unterminated string", lineno);
            buf = salloc((size_t)(j - i));
            memcpy(buf, line + i + 1, (size_t)(j - i - 1));
            buf[j - i - 1] = 0;
            addtok(out, &nt, lineno, T_STR, buf, "");
            i = j + 1;
            continue;
        }
        if (i + 1 < n) {
            char two[3];
            int m;
            two[0] = line[i];
            two[1] = line[i + 1];
            two[2] = 0;
            for (m = 0; ops2[m]; m++)
                if (strcmp(two, ops2[m]) == 0)
                    break;
            if (ops2[m]) {
                const char *t = ops2[m];
                if (strcmp(t, "=<") == 0)
                    t = "<=";
                else if (strcmp(t, "=>") == 0)
                    t = ">=";
                else if (strcmp(t, "><") == 0)
                    t = "<>";
                addtok(out, &nt, lineno, T_OP, t, t);
                i += 2;
                continue;
            }
        }
        if (strchr(ops1, c) != NULL) {
            char *t = salloc(2);
            t[0] = c;
            t[1] = 0;
            addtok(out, &nt, lineno, T_OP, t, t);
            i++;
            continue;
        }
        mm_error("line %d: unexpected character %s", lineno, char_repr(c));
    }
    return nt;
}

/* MMBasic string constant -> C initialiser with the length byte. */
char *c_string_literal(const char *s)
{
    size_t n = strlen(s);
    char *body;
    size_t i, j = 0;

    if (n > 255)
        mm_error("string constant longer than 255 characters");
    body = salloc(n * 4 + 1);
    for (i = 0; i < n; i++) {
        unsigned char o = (unsigned char)s[i];
        if (o == '"') {
            body[j++] = '\\';
            body[j++] = '"';
        } else if (o == '\\') {
            body[j++] = '\\';
            body[j++] = '\\';
        } else if (o >= 32 && o < 127) {
            body[j++] = (char)o;
        } else {
            j += (size_t)sprintf(body + j, "\\%03o", o);
        }
    }
    body[j] = 0;
    /* two adjacent literals so the length byte can never be swallowed
     * by a following hex/octal digit */
    return sfmt("\"\\%03o\" \"%s\"", (unsigned)n, body);
}

/* 'nbr%' -> ("nbr", TY_I).  Canonical lower-case name; *ty gets the
 * suffix type or TY_NONE. */
char *split_suffix(const char *word, int *ty)
{
    size_t n = strlen(word);
    char *p;

    if (n > 0 && (word[n - 1] == '$' || word[n - 1] == '%'
                  || word[n - 1] == '!')) {
        *ty = word[n - 1] == '$' ? TY_S
            : word[n - 1] == '%' ? TY_I : TY_F;
        p = salloc(n);
        memcpy(p, word, n - 1);
        p[n - 1] = 0;
        return lower(p);
    }
    *ty = TY_NONE;
    return lower(word);
}

static char *dots_to_dunder(const char *pfx, const char *name)
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

char *cvar(const char *name)
{
    return dots_to_dunder("v_", name);
}

/* A global CONST's C name.  Its OWN prefix, not cvar's: a global CONST
 * is emitted as a #define and a macro has no scope, so with both on
 * `v_` a LOCAL named the same as a global CONST had its declaration
 * rewritten by the macro and the C did not compile.  MMBasic simply
 * shadows - findvar looks in the local table first - and the two
 * prefixes are how that shadowing survives into C. */
char *cconst(const char *name)
{
    return dots_to_dunder("k_", name);
}

/* GP8 read as a value -> 8, the pin it names.  -1 if it is not one.
 *
 * MMBasic resolves these in getpinarg() rather than in the expression
 * parser, and says why: "GPn is not a valid expression".  It has to be
 * a special case there because MMBasic's pin numbers are CONNECTOR pins
 * and GP8 is a GPIO, so the name needs PINMAP.  Here the two are the
 * same number - mmb_gpio.h chose the GPIO numbering and gave its
 * reasons - so the name resolves to the number itself, once, for every
 * place a pin is written: SETPIN gp8, PIN(gp8), PORT(GP12,2,...).
 *
 * A DECLARED variable of that name still wins, which is what `known` is
 * for.  Without this the name was not an error either: with OPTION
 * EXPLICIT off it became an implied global, so `Pin(GP8)` silently read
 * GP0 - a wrong pin rather than a refusal.
 */
int gp_pin(const char *word, const struct sym *known)
{
    int n = 0;
    const char *p;

    if (known != NULL)
        return -1;
    if (word[0] == 0 || word[1] == 0 || word[2] == 0)
        return -1;
    if ((word[0] != 'G' && word[0] != 'g')
        || (word[1] != 'P' && word[1] != 'p'))
        return -1;
    for (p = word + 2; *p; p++) {
        if (*p < '0' || *p > '9')
            return -1;
        n = n * 10 + (*p - '0');
        if (n > 47)
            return -1;      /* GP99 is far more likely to be a variable */
    }
    return n;
}

char *clabel(const char *name)
{
    return dots_to_dunder("L_", name);
}

/* True when an emitted expression is a C constant expression.
 *
 * An array is declared in C with its bounds written into the type, so a
 * DIM bound has to fold at compile time.  CONST substitutes its value
 * textually and literals are literals, so the test is simply whether
 * any letter in the text belongs to something other than a number: a
 * variable arrives as v_<name>, a call as mm_<name>. */
/* const_c_expr with string literals allowed: is this expression a
 * compile-time constant once quoted spans are ignored?  The test that
 * decides whether a global CONST can be a #define - one that cannot
 * (it calls into the runtime, like Mm.Device$) is materialised into a
 * hidden global instead, assigned ONCE where the CONST statement
 * stands, exactly as cmd_const's DoExpression evaluates once.  The
 * #define form re-evaluated the expression at EVERY use: robots'
 * LCD_DISPLAY called mm_device() twice per test, each call parking a
 * scratch string nothing ever released, and the pool died in
 * fade_in. */
int const_or_literal_expr(const char *text)
{
    char *out = salloc(strlen(text) + 1);
    int i = 0, j = 0;

    while (text[i]) {
        if (text[i] == '"') {
            i++;
            while (text[i] && text[i] != '"')
                i += (text[i] == '\\' && text[i + 1]) ? 2 : 1;
            if (text[i])
                i++;
            continue;
        }
        out[j++] = text[i++];
    }
    out[j] = 0;
    return const_c_expr(out);
}

int const_c_expr(const char *text)
{
    int i = 0;

    while (text[i]) {
        int c = text[i];
        if (is_digit_c(c)) {
            i++;                       /* a numeric literal, with any */
            while (text[i] && ((is_alpha(text[i]) && text[i] != '_')
                               || is_digit_c(text[i])
                               || text[i] == '.'
                               || ((text[i] == '+' || text[i] == '-')
                                   && (text[i - 1] == 'e'
                                       || text[i - 1] == 'E'))))
                i++;                   /* 0x prefix, exponent or suffix */
            continue;
        }
        if (is_alpha(c))               /* is_alpha covers '_' as well */
            return 0;
        i++;
    }
    return 1;
}

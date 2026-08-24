#
# mmb2c.py - an MMBasic (PicoMite) to C translator.
#
# Written to run under MicroPython on the Pico itself as well as under
# CPython.  Deliberately avoids: re, f-strings, typing, dataclasses,
# collections, enum and anything else MicroPython may not ship.
#
#   Usage:   python3 mmb2c.py source.bas [-o out.c] [--report]
#   On the Pico:   import mmb2c ; mmb2c.convert("prog.bas", "prog.c")
#
# The generated C needs mmb_runtime.h / mmb_runtime.c alongside it.
#
# Supported for now:
#   OPTION DEFAULT / OPTION EXPLICIT / OPTION BASE
#   DIM, LOCAL, STATIC, CONST  (with AS type, type-prefix, suffixes,
#                               initialisers, arrays and array init lists)
#   assignment (with or without LET), MID$()= , PRINT (; , TAB())
#   IF/THEN/ELSEIF/ELSE/ENDIF   (block and single line)
#   FOR/NEXT/STEP, DO/LOOP [WHILE|UNTIL], WHILE/WEND,
#   SELECT CASE/CASE/CASE ELSE/END SELECT (incl. TO ranges and IS tests)
#   EXIT FOR/DO/SUB/FUNCTION, GOTO + labels, END
#   SUB/END SUB, FUNCTION/END FUNCTION, by-reference and BYVAL args,
#   array parameters, calls with omitted or missing arguments.
#
# The scope model is MMBasic's:  a variable is either GLOBAL - created by
# DIM or, far more often, created implicitly the first time it is touched
# anywhere in the program - or LOCAL to one sub/function, which requires
# a LOCAL (or STATIC) statement or being a parameter.  Finding the
# implied globals is what pass 2 below is for.
#

VERSION = "0.1"

# ----------------------------------------------------------------- types

TY_F = 'f'      # MMFLOAT   (double)   - MMBasic's default
TY_I = 'i'      # MMINTEGER (int64_t)
TY_S = 's'      # string, MMBasic layout: [len][data...][NUL]

CTYPE = {TY_F: 'MMFLOAT', TY_I: 'MMINTEGER', TY_S: 'char'}
TYNAME = {TY_F: 'FLOAT', TY_I: 'INTEGER', TY_S: 'STRING'}

# MM_STRLEN in mmb_runtime.h: the characters a string can hold, and the
# LENGTH an array element gets when the program does not say.
MM_STRLEN = 255

# OPTION ANGLE DEGREES: MMBasic's RADCONV (Functions.h:38), to its own
# digits.  SIN/COS/TAN divide by it, ATN/ATAN2/ASIN/ACOS multiply by it
# - the same operations in the same order as the reference, so a
# side-by-side agrees to the last bit.
RADCONV = '57.2957795130823229'

# ----------------------------------------------------------------- tokens

T_ID = 1
T_NUM = 2
T_STR = 3
T_OP = 4

# Words that can never be a variable name (manual: "Variables and
# Expressions").  Used so the trawl for implied globals does not invent
# a variable called STEP or TO.
KEYWORDS = (
    'THEN', 'ELSE', 'GOTO', 'GOSUB', 'TO', 'STEP', 'FOR', 'WHILE', 'UNTIL',
    'LOAD', 'MOD', 'NOT', 'AND', 'OR', 'XOR', 'AS', 'INV', 'IS', 'CASE',
    'SELECT', 'IF', 'ENDIF', 'END', 'SUB', 'FUNCTION', 'EXIT', 'DIM',
    'LOCAL', 'STATIC', 'CONST', 'PRINT', 'LET', 'DO', 'LOOP', 'WEND',
    'NEXT', 'OPTION', 'REM', 'BYVAL', 'BYREF', 'INTEGER', 'FLOAT',
    'STRING', 'CALL', 'RETURN', 'BASE', 'EXPLICIT', 'DEFAULT', 'NONE',
    'OPEN', 'CLOSE', 'INPUT', 'OUTPUT', 'APPEND', 'RANDOM', 'SEEK',
    'KILL', 'RENAME', 'MKDIR', 'RMDIR', 'CHDIR', 'COPY', 'FILES', 'ALL',
    'DATA', 'READ', 'RESTORE', 'SORT', 'CONTINUE', 'INC', 'CAT', 'ERASE',
    'CLEAR', 'PAUSE', 'ERROR', 'ARRAY', 'SAVE', 'PRESERVE', 'LONGSTRING',
)

# Statements that take no arguments at all, so a ':' after one is a
# statement separator and never a label definition.
#
# The rest of the statement words do not need to be here: every one of
# them is followed by an argument, so "NAME :" cannot arise.  CLS is the
# only one that can stand alone, and without this "CLS : PRINT x"
# defines a label called CLS and drops the clear without a word.
BARE_STATEMENTS = ('CLS',)

# Built-in functions we can translate.  name -> (minargs, maxargs)
BUILTINS = {
    # name: (min args, max args)
    'ABS': (1, 1), 'INT': (1, 1), 'FIX': (1, 1), 'CINT': (1, 1),
    'SGN': (1, 1), 'SQR': (1, 1), 'SIN': (1, 1), 'COS': (1, 1),
    'TAN': (1, 1), 'ATN': (1, 1), 'ASIN': (1, 1), 'ACOS': (1, 1),
    'ATAN2': (2, 2), 'DEG': (1, 1), 'RAD': (1, 1),
    'LOG': (1, 1), 'EXP': (1, 1),
    'RND': (0, 1), 'PI': (0, 0), 'MAX': (2, 8), 'MIN': (2, 8),
    'LEN': (1, 1), 'ASC': (1, 1), 'VAL': (1, 1), 'INSTR': (2, 3),
    'TAB': (1, 1), 'TIMER': (0, 0), 'BIT': (2, 2), 'BYTE': (2, 2),
    'CHR$': (1, 1), 'LEFT$': (2, 2), 'RIGHT$': (2, 2), 'MID$': (2, 3),
    'STR$': (1, 4), 'HEX$': (1, 2), 'OCT$': (1, 2), 'BIN$': (1, 2),
    'UCASE$': (1, 1), 'LCASE$': (1, 1), 'SPACE$': (1, 1),
    'STRING$': (2, 2), 'LTRIM$': (1, 1), 'RTRIM$': (1, 1),
    'FORMAT$': (1, 2),
    'DATE$': (0, 0), 'TIME$': (0, 0), 'CWD$': (0, 0), 'INKEY$': (0, 0),
    'EOF': (1, 1), 'LOC': (1, 1), 'LOF': (1, 1), 'INPUT$': (2, 2),
    # these are parsed by hand because an argument may be a bare keyword
    'CHOICE': (3, 3), 'BOUND': (1, 2), 'TRIM$': (1, 3), 'FIELD$': (2, 4),
    'DATETIME$': (1, 1), 'DAY$': (1, 1), 'EPOCH': (1, 1),
    'BIN2STR$': (2, 3), 'STR2BIN': (2, 3), 'RGB': (1, 3), 'MATH': (1, 1),
    'PIXEL': (2, 2), 'MAP': (1, 1), 'PIN': (1, 1), 'SPI': (1, 1),
    'PORT': (2, 16), 'FLAG': (1, 1), 'TEMPR': (1, 2),
    'PULSIN': (2, 4), 'DISTANCE': (1, 2),
    'MM.HRES': (0, 0), 'MM.VRES': (0, 0), 'MM.SPISPEED': (0, 0),
    'MM.ONEWIRE': (0, 0), 'MM.I2C': (0, 0),
    'POS': (0, 0),
    'MM.ERRNO': (0, 0), 'MM.ERRMSG$': (0, 0),
    'MM.MESSAGE$': (0, 0), 'MM.ADDRESS$': (0, 0),
    'MM.VER': (0, 0), 'MM.DEVICE$': (0, 0), 'MM.CMDLINE$': (0, 0),
    'MM.FONTHEIGHT': (0, 0), 'MM.FONTWIDTH': (0, 0),
    'MM.HPOS': (0, 0), 'MM.VPOS': (0, 0),
    'MM.INFO': (1, 1), 'MM.INFO$': (1, 1),
    'KEYDOWN': (1, 1),
    'PEEK': (1, 1), 'SPRITE': (1, 3),
    'DIR$': (0, 2),
    'LLEN': (1, 1), 'LGETSTR$': (3, 3), 'LGETBYTE': (2, 2),
    'LINSTR': (2, 3), 'LCOMPARE': (2, 2), 'LINPUT': (3, 3),
    'JSON$': (2, 2),
}

# built-ins whose arguments cannot be parsed as plain expressions
RAWARG = ('CHOICE', 'BOUND', 'TRIM$', 'DATETIME$', 'DAY$', 'EPOCH',
          'BIN2STR$', 'STR2BIN', 'RGB', 'MATH',
          'MM.INFO', 'MM.INFO$', 'PEEK', 'SPRITE',
          'EOF', 'LOC', 'LOF', 'INPUT$', 'DIR$',
          'LLEN', 'LGETSTR$', 'LGETBYTE', 'LINSTR', 'LCOMPARE', 'LINPUT',
          'JSON$')

# built-ins that return a string (and therefore consume a scratch buffer)
STRFUNCS = ('CHR$', 'LEFT$', 'RIGHT$', 'MID$', 'STR$', 'HEX$', 'OCT$',
            'BIN$', 'UCASE$', 'LCASE$', 'SPACE$', 'STRING$', 'LTRIM$',
            'RTRIM$', 'TAB', 'FORMAT$', 'TRIM$', 'FIELD$', 'DATE$',
            'TIME$', 'DATETIME$', 'DAY$', 'BIN2STR$', 'INPUT$', 'DIR$',
            'CWD$', 'INKEY$', 'LGETSTR$', 'JSON$')

# BIN2STR$ / STR2BIN type names -> the runtime's MM_B_* constants
BINTYPES = ('INT64', 'UINT64', 'INT32', 'UINT32', 'INT16', 'UINT16',
            'INT8', 'UINT8', 'SINGLE', 'DOUBLE')

# RGB() colour shortcuts, values taken from graphics/Draw.h
RGBNAMES = {
    'WHITE': 0xFFFFFF, 'YELLOW': 0xFFFF00, 'LILAC': 0xFF80FF,
    'BROWN': 0xFF8000, 'FUCHSIA': 0xFF40FF, 'RUST': 0xFF4000,
    'MAGENTA': 0xFF00FF, 'RED': 0xFF0000, 'CYAN': 0x00FFFF,
    'GREEN': 0x00FF00, 'CERULEAN': 0x0080FF, 'MIDGREEN': 0x008000,
    'COBALT': 0x0040FF, 'MYRTLE': 0x004000, 'BLUE': 0x0000FF,
    'BLACK': 0x000000, 'GRAY': 0x808080, 'GREY': 0x808080,
    'LITEGRAY': 0xD2D2D2, 'LIGHTGRAY': 0xD2D2D2, 'LIGHTGREY': 0xD2D2D2,
    'ORANGE': 0xFFA500, 'PINK': 0xFFA0AB, 'GOLD': 0xFFD700,
    'SALMON': 0xFA8072, 'BEIGE': 0xF5F5DC,
}

# the scalar members of the MATH() family
MATHFUNCS = {'COSH': 1, 'SINH': 1, 'TANH': 1, 'LOG10': 1, 'ATAN3': 2}

# the MATH() members that reduce a whole array to one number
MATHARRAY = ('SUM', 'MEAN', 'SD', 'MAX', 'MIN', 'MEDIAN')

# MATH(CRCn v [,length [,poly [,start [,end [,revIn [,revOut]]]]]]).
# Neither table above can hold it - MATHFUNCS is a fixed argument
# count and MATHARRAY is a whole-array reduction - so it is a branch of
# its own, like MATH(BASE64.  Width -> (default polynomial, argument
# range), from MATHS.h:44/58/66/82 and fun_math's four branches.
CRCWIDTH = {
    'CRC8':  (8,  '0x07'),
    'CRC12': (12, '0x80D'),
    'CRC16': (16, '0x1021'),
    'CRC32': (32, '0x04C11DB7'),
}

OPS3 = ()
OPS2 = ('<=', '>=', '<>', '=<', '=>', '><', '<<', '>>')
# '.' is an operator ONLY when it survives identifier scanning - dots
# inside a name are eaten greedily by is_idchar, so a '.' token can
# only arise after ')' and the like: the arr(i).member form.
OPS1 = '+-*/\\^=<>(),;:?@#.'


def is_alpha(c):
    return ('a' <= c <= 'z') or ('A' <= c <= 'Z') or c == '_'


def is_digit(c):
    return '0' <= c <= '9'


def is_idchar(c):
    return is_alpha(c) or is_digit(c) or c == '.'


def is_hexd(c):
    return is_digit(c) or ('a' <= c <= 'f') or ('A' <= c <= 'F')


class MMError(Exception):
    pass


def nonzero_literal(code):
    """Is this emitted operand a number that cannot be zero?

    Only a plain literal counts - the text as it will appear in the C.
    A divisor like 180.0 or 86400.0 needs no divide-by-zero test, and on
    the board that test is a call across the VM boundary, so knowing the
    answer here is worth the few lines."""
    t = code.strip().rstrip('Ll')
    if not t or (t[0] not in '0123456789'
                 and not (t[0] == '-' and len(t) > 1)):
        return False
    for ch in t:
        if ch not in '0123456789.eE+-':
            return False
    try:
        return float(t) != 0.0
    except ValueError:
        return False


def float_form_of_int_literal(code):
    """A plain decimal integer literal, written as a float.

    '3600LL' becomes '3600.0' - the same double either way, since up to
    15 digits every integer is exact - but nonzero_literal can read it,
    so dividing by it needs no zero test.  Hex (from &H) keeps its cast,
    '0x10.0' being no number, and so does a leading zero, which C reads
    as octal.  None for anything that is not simply digits."""
    t = code.strip().rstrip('Ll')
    if not t or len(t) > 15 or (len(t) > 1 and t[0] == '0'):
        return None
    for ch in t:
        if not is_digit(ch):
            return None
    return t + '.0'


def boolean_expr(code):
    """True when an emitted expression is already 0-or-1: a single
    comparison at the top of its tree.  Only then may a condition skip
    the '(...) != 0' wrapper; everything else keeps it, including the
    AND/OR combinations (bitwise on integers in MMBasic) and bare
    numbers.  Textual, like nonzero_literal: a comparison operator at
    parenthesis depth 1 of the emitted form is the top of the tree,
    string literals are skipped, '->' and shifts are not comparisons,
    and a '?' at depth 1 is a ternary whose value needs the test."""
    n = len(code)
    if n == 0 or code[0] != '(':
        return False
    depth = 0
    seen = False
    i = 0
    while i < n:
        ch = code[i]
        if ch == '"':
            i += 1
            while i < n and code[i] != '"':
                if code[i] == '\\':
                    i += 1
                i += 1
        elif ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        elif depth == 1:
            if ch == '?':
                return False
            if ch == '<' or ch == '>':
                nxt = code[i + 1] if i + 1 < n else ''
                if nxt == ch:
                    i += 1
                elif ch == '>' and i > 0 and code[i - 1] == '-':
                    pass
                else:
                    seen = True
                    if nxt == '=':
                        i += 1
            elif (ch == '=' or ch == '!') and i + 1 < n \
                    and code[i + 1] == '=':
                seen = True
                i += 1
        i += 1
    return seen and depth == 0


def cblock_safe(text):
    """Make text safe to sit inside a C /* */ comment."""
    out = text.replace('*/', '* /')
    return ''.join(c if 32 <= ord(c) < 127 else ' ' for c in out)


def websub_norm(s):
    """A page expression's table key: verbatim inside a "string
    literal", upcased with whitespace dropped outside one.
    mm_webpg_next (mmb_webs.h) applies the same rule at run time, and
    the two MUST agree or a page's expressions stop matching."""
    out = []
    q = False
    for c in s:
        if c == '"':
            q = not q
            out.append(c)
            continue
        if q:
            out.append(c)
            continue
        if c in ' \t\r\n':
            continue
        out.append(c.upper())
    return ''.join(out)


def websub_scan(text):
    """Every {expression} in a page, raw text in first-seen order,
    deduped by normalised key.  '{{' is a literal '{', 0x1A is xmodem
    padding, and an expression ends at the FIRST '}' - string-blind,
    exactly as the reference collects it (MMtcpserver.c:656-664) and
    exactly as the engine collects it."""
    keys, raws, seen = [], [], {}
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        i += 1
        if c != '{':
            continue
        if i < n and text[i] == '{':
            i += 1
            continue
        j = i
        while j < n and text[j] != '}':
            j += 1
        raw = text[i:j].replace('\x1a', '')
        i = j + 1
        key = websub_norm(raw)
        if key == '' or key in seen:
            continue
        seen[key] = True
        keys.append(key)
        raws.append(raw)
    return keys, raws


# ------------------------------------------------- OPTION ESCAPE
#
# The reference decodes escape sequences at string-literal EVALUATION
# (MMBasic.c evaluate(), guarded by OptionEscape), so once OPTION
# ESCAPE has executed every literal in the program decodes.  A
# translator has no run time, so the rule here is positional: literals
# textually after the OPTION ESCAPE statement decode, literals before
# it do not.  For the way every real program writes it - OPTION ESCAPE
# at the top - the two rules describe the same program.  There is no
# OFF form (MM_Misc.c only ever sets OptionEscape true).
#
# The closing quote is found exactly as before: the reference scans
# with strchr(p, '"') even in escape mode, which is why \q exists - a
# literal quote cannot be written any other way.  Only the body decode
# changes, never the token boundaries.
#
# ESCAPE_LINE/ESCAPE_COL are set once per source by scan_escape(), so
# every tokenize() of any line answers consistently no matter which
# pre-scan pass asks, or in what order it walks the lines.

ESCAPE_LINE = 0     # 1-based line of the OPTION ESCAPE statement, 0 = off
ESCAPE_COL = 0      # column just past the ESCAPE keyword on that line


def scan_escape(lines):
    """Find the first OPTION ESCAPE statement; set the positional gate."""
    global ESCAPE_LINE, ESCAPE_COL
    ESCAPE_LINE = 0
    ESCAPE_COL = 0
    for idx in range(len(lines)):
        line = lines[idx]
        n = len(line)
        i = 0
        at_stmt = True
        while i < n:
            c = line[i]
            if c == "'" or c == '\x1a':
                break
            if c in ' \t\r\n':
                i += 1
                continue
            if c == '"':
                i += 1
                while i < n and line[i] != '"':
                    i += 1
                i += 1
                at_stmt = False
                continue
            if c == ':':
                at_stmt = True
                i += 1
                continue
            if is_alpha(c):
                j = i
                while j < n and is_idchar(line[j]):
                    j += 1
                word = line[i:j].upper()
                if word == 'REM':
                    break
                if at_stmt and word == 'OPTION':
                    k = j
                    while k < n and line[k] in ' \t':
                        k += 1
                    m = k
                    while m < n and is_idchar(line[m]):
                        m += 1
                    if line[k:m].upper() == 'ESCAPE':
                        ESCAPE_LINE = idx + 1
                        ESCAPE_COL = m
                        return
                i = j
                at_stmt = False
                continue
            at_stmt = False
            i += 1


_ESC_CHARS = '\\abefnqrtv'
_ESC_VALUES = '\\\a\b\x1b\f\n"\r\t\v'


def mm_unescape(body, lineno):
    """The reference's decode loop (MMBasic.c evaluate), to the letter:
    \\ddd is three DECIMAL digits despite the comment there saying
    octal, \\&hh is two hex digits, single-character escapes come from
    a fixed table with \\q for the quote, an unknown escape passes its
    character through, a backslash that is the last character before
    the closing quote stays literal (the tp > p+1 test), and a decoded
    zero byte is an error - an M-string could hold one, but the
    reference refuses it and so do we."""
    out = []
    i = 0
    n = len(body)
    while i < n:
        c = body[i]
        if c == '\\' and i + 1 < n:
            i += 1
            c = body[i]
            if ('0' <= c <= '9' and i + 2 < n
                    and '0' <= body[i + 1] <= '9'
                    and '0' <= body[i + 2] <= '9'):
                v = ((ord(c) - 48) * 100 + (ord(body[i + 1]) - 48) * 10
                     + (ord(body[i + 2]) - 48))
                i += 3
                if v == 0:
                    raise MMError("line %d: OPTION ESCAPE: NUL in string"
                                  " literal" % lineno)
                out.append(chr(v & 0xFF))
            elif (c == '&' and i + 2 < n
                    and is_hexd(body[i + 1]) and is_hexd(body[i + 2])):
                v = int(body[i + 1:i + 3], 16)
                i += 3
                if v == 0:
                    raise MMError("line %d: OPTION ESCAPE: NUL in string"
                                  " literal" % lineno)
                out.append(chr(v))
            else:
                k = _ESC_CHARS.find(c)
                out.append(_ESC_VALUES[k] if k >= 0 else c)
                i += 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def tokenize(line, lineno):
    """Turn one source line into a list of (kind, text, upper) tuples."""
    out = []
    i = 0
    n = len(line)
    while i < n:
        c = line[i]
        if c == ' ' or c == '\t' or c == '\r' or c == '\n':
            i += 1
            continue
        if c == '\x1a':            # a DOS end of file marker: stop here
            break
        if c == "'":                       # comment to end of line
            break
        if is_alpha(c):
            j = i
            while j < n and is_idchar(line[j]):
                j += 1
            if j < n and line[j] in '$%!':
                j += 1
            word = line[i:j]
            up = word.upper()
            if up == 'REM':                # comment
                break
            out.append((T_ID, word, up))
            i = j
            continue
        if is_digit(c) or (c == '.' and i + 1 < n and is_digit(line[i + 1])):
            j = i
            isf = False
            while j < n and is_digit(line[j]):
                j += 1
            if j < n and line[j] == '.':
                isf = True
                j += 1
                while j < n and is_digit(line[j]):
                    j += 1
            if j < n and line[j] in 'eE':
                k = j + 1
                if k < n and line[k] in '+-':
                    k += 1
                if k < n and is_digit(line[k]):
                    isf = True
                    j = k
                    while j < n and is_digit(line[j]):
                        j += 1
            txt = line[i:j]
            out.append((T_NUM, txt, 'F' if isf else 'I'))
            i = j
            continue
        if c == '&':                       # &H &O &B  -> integer
            j = i + 1
            if j < n and line[j] in 'hHoObB':
                base = {'h': 16, 'o': 8, 'b': 2}[line[j].lower()]
                j += 1
                k = j
                while k < n and is_hexd(line[k]):
                    k += 1
                if k == j:
                    raise MMError("line %d: bad &-constant" % lineno)
                val = int(line[j:k], base)
                # &H, &O and &B constants are 64-bit unsigned; emit them in
                # hex so a value above 2^63 does not overflow the literal
                out.append((T_NUM, '((MMINTEGER)0x%XULL)' % (val & ((1 << 64) - 1)),
                            'H'))
                i = k
                continue
            raise MMError("line %d: bad & constant" % lineno)
        if c == '"':
            j = i + 1
            buf = []
            while j < n and line[j] != '"':
                buf.append(line[j])
                j += 1
            if j >= n:
                raise MMError("line %d: unterminated string" % lineno)
            s = ''.join(buf)
            if ESCAPE_LINE and (lineno > ESCAPE_LINE
                                or (lineno == ESCAPE_LINE
                                    and i > ESCAPE_COL)):
                s = mm_unescape(s, lineno)
            out.append((T_STR, s, ''))
            i = j + 1
            continue
        two = line[i:i + 2]
        if two in OPS2:
            if two == '=<':
                two = '<='
            elif two == '=>':
                two = '>='
            elif two == '><':
                two = '<>'
            out.append((T_OP, two, two))
            i += 2
            continue
        if c in OPS1:
            out.append((T_OP, c, c))
            i += 1
            continue
        raise MMError("line %d: unexpected character %r" % (lineno, c))
    return out


def c_string_literal(s):
    """MMBasic string constant -> C initialiser with the length byte."""
    if len(s) > 255:
        raise MMError("string constant longer than 255 characters")
    body = []
    for ch in s:
        o = ord(ch)
        if ch == '"':
            body.append('\\"')
        elif ch == '\\':
            body.append('\\\\')
        elif 32 <= o < 127:
            body.append(ch)
        else:
            body.append('\\%03o' % (o & 0xFF))
    # kept as two adjacent literals so the length byte can never be
    # swallowed by a following hex/octal digit
    return '"\\%03o" "%s"' % (len(s), ''.join(body))


# ----------------------------------------------------------------- symbols

class Sym(object):
    __slots__ = ('name', 'ty', 'acc', 'dims', 'is_const', 'is_array',
                 'is_param', 'byref', 'is_static', 'where', 'implied',
                 'declared_in', 'disp', 'has_init', 'stype', 'dynamic',
                 'bacc', 'const_runtime', 'slen')

    def __init__(self, name, ty, acc):
        self.name = name          # canonical: lower case, no suffix
        self.disp = name          # as the programmer spelled it
        self.ty = ty
        self.stype = None         # canonical TYPE name when a struct
        self.acc = acc            # C text used to read/write it
        self.dims = None          # list of C size expressions
        self.is_const = False
        self.is_array = False
        self.is_param = False
        self.byref = False
        self.is_static = False
        self.where = 0            # source line first seen
        self.implied = False
        self.has_init = False
        self.declared_in = ''     # '' = main line, else routine name
        # An array whose bounds are only known at run time.  It is held
        # exactly as an array PARAMETER is - a flat pointer plus a
        # bounds table - so index(), array_flat() and BOUND() take the
        # same branch for both.  bacc is the C text of that table.
        self.dynamic = False
        self.bacc = ''
        # A global CONST whose expression is not a compile-time
        # constant: a hidden global assigned once where the CONST
        # statement stands, never a #define (see do_const)
        self.const_runtime = False
        # DIM s$(n) LENGTH m on an ARRAY: the element stride, which is
        # m + 1 and is part of the PROGRAM'S VIEW OF MEMORY, not just a
        # saving - findvar returns val.s + nbr * (size + 1)
        # (MMBasic.c:4924), so a program walking the array with
        # PEEK(VARADDR a$()) is entitled to that spacing.  None = the
        # default MM_STRSZ element, which carries a trailing NUL.
        self.slen = None


class TypeMember(object):
    """One member of a TYPE.  esize is the element size in bytes and
    count the number of elements (1 unless the member is an array), so
    offset + esize * count is where the next member starts from."""
    __slots__ = ('name', 'disp', 'ty', 'stype', 'slen', 'dims',
                 'count', 'offset', 'esize')

    def __init__(self, name):
        self.name = name
        self.disp = name
        self.ty = None            # TY_* for plain members, None for struct
        self.stype = None         # canonical type name for struct members
        self.slen = 255           # STRING members: LENGTH
        self.dims = None          # list of int bounds, or None
        self.count = 1
        self.offset = 0
        self.esize = 0


class TypeDef(object):
    """A TYPE ... END TYPE definition, laid out exactly as the firmware
    lays it out (ParseStructMember + GetStructAlignment): numeric and
    struct members start 8-aligned, strings are unaligned, and the
    total is rounded to 8 only when something numeric is inside.  See
    TYPE-SPEC.md for the full contract."""
    __slots__ = ('name', 'disp', 'members', 'byname', 'total', 'numeric',
                 'where')

    def __init__(self, name):
        self.name = name
        self.disp = name
        self.members = []
        self.byname = {}
        self.total = 0
        self.numeric = False      # anything numeric anywhere inside
        self.where = 0

    def add(self, m, types):
        if m.ty == TY_S:
            m.esize = m.slen + 1
        elif m.stype is not None:
            inner = types[m.stype]
            m.esize = inner.total
            if inner.numeric:
                self.numeric = True
        else:
            m.esize = 8
            self.numeric = True
        off = self.total
        if (m.ty in (TY_I, TY_F) or m.stype is not None) and off % 8:
            off = (off // 8 + 1) * 8
        m.offset = off
        m.count = 1
        if m.dims is not None:
            for d in m.dims:
                m.count *= d + 1
        self.total = off + m.esize * m.count
        self.members.append(m)
        self.byname[m.name] = m

    def close(self):
        if self.numeric and self.total % 8:
            self.total = (self.total // 8 + 1) * 8


class Routine(object):
    __slots__ = ('name', 'cname', 'is_func', 'ty', 'params', 'locals',
                 'statics', 'line', 'gtouch', 'disp', 'local_order',
                 'heap_locals')

    def __init__(self, name, is_func):
        self.name = name
        self.disp = name
        self.cname = 'f_' + name.replace('.', '__')
        self.is_func = is_func
        self.ty = TY_F
        self.params = []          # list of Sym
        self.locals = {}          # canonical name -> Sym
        self.local_order = []     # names in declaration order, so the C
                                  # comes out the same on any Python
        self.statics = []         # list of Sym (subset of locals)
        self.heap_locals = False  # has LOCAL arrays or strings, so its
                                  # invocations carry a heap block
        self.line = 0
        self.gtouch = {}          # global name -> first line touched here


# ----------------------------------------------------------------- helpers

def split_suffix(word):
    """'nbr%' -> ('nbr', TY_I).  Returns (canonical_lower, ty_or_None)."""
    if word.endswith('$'):
        return word[:-1].lower(), TY_S
    if word.endswith('%'):
        return word[:-1].lower(), TY_I
    if word.endswith('!'):
        return word[:-1].lower(), TY_F
    return word.lower(), None


def gp_pin(word, known):
    """GP8 read as a value -> 8, the pin it names.  None if it is not one.

    MMBasic resolves these in getpinarg() rather than in the expression
    parser, and says why: "GPn is not a valid expression".  It has to be
    a special case there because MMBasic's pin numbers are CONNECTOR
    pins and GP8 is a GPIO, so the name needs PINMAP.  Here the two are
    the same number - mmb_gpio.h chose the GPIO numbering and gave its
    reasons - so the name resolves to the number itself and can be done
    once, here, for every place a pin is written: SETPIN gp8, PIN(gp8),
    PORT(GP12,2,GP11,2,...).

    A DECLARED variable of that name still wins, which is what `known`
    is for.  Without this the name was not an error either: with OPTION
    EXPLICIT off it became an implied global, so `Pin(GP8)` silently
    read GP0 - a wrong pin rather than a refusal, the divergence class
    that outranks a missing feature.
    """
    if known is not None:
        return None
    n = 0
    if len(word) < 3 or word[0] not in 'Gg' or word[1] not in 'Pp':
        return None
    for c in word[2:]:
        if c < '0' or c > '9':
            return None
        n = n * 10 + (ord(c) - 48)
    # 48 GPIOs on the RP2350B.  Out of range is left alone rather than
    # resolved: GP99 is far more likely to be someone's variable.
    return n if n < 48 else None


def cvar(name):
    return 'v_' + name.replace('.', '__')


def cconst(name):
    """A global CONST's C name.

    Its OWN prefix, not cvar's.  A global CONST is emitted as a #define,
    and a macro has no scope: with both on `v_`, a LOCAL named the same
    as a global CONST had its declaration rewritten by the macro and the
    C did not compile.  MMBasic simply shadows - findvar looks in the
    local table first - and the two prefixes are how that shadowing
    survives into C.
    """
    return 'k_' + name.replace('.', '__')


def clabel(name):
    return 'L_' + name.replace('.', '__')


def const_or_literal_expr(text):
    """const_c_expr with string literals allowed: is this expression a
    compile-time constant once quoted spans are ignored?  The test that
    decides whether a global CONST can be a #define - one that cannot
    (it calls into the runtime, like Mm.Device$) is materialised into a
    hidden global instead, assigned ONCE where the CONST statement
    stands, exactly as cmd_const's DoExpression evaluates once.  The
    #define form re-evaluated the expression at EVERY use: robots'
    LCD_DISPLAY called mm_device() twice per test, each call parking a
    scratch string nothing ever released, and the pool died in
    fade_in."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        if text[i] == '"':
            i += 1
            while i < n and text[i] != '"':
                i += 2 if text[i] == '\\' else 1
            i += 1
            continue
        out.append(text[i])
        i += 1
    return const_c_expr(''.join(out))


def const_c_expr(text):
    """True when an emitted expression is a C constant expression.

    An array is declared in C with its bounds written into the type, so
    a DIM bound has to fold at compile time.  CONST substitutes its
    value textually and literals are literals, so the test is simply
    whether any letter in the text belongs to something other than a
    number: a variable arrives as v_<name>, a call as mm_<name>."""
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c.isdigit():
            i += 1                     # a numeric literal, with any
            while i < n and (text[i].isalnum() or text[i] == '.'
                             or (text[i] in '+-' and text[i - 1] in 'eE')):
                i += 1                 # 0x prefix, exponent or suffix
            continue
        if c.isalpha() or c == '_':
            return False
        i += 1
    return True


# ----------------------------------------------------------------- the guts

class Conv(object):

    def __init__(self, lines, srcname='program'):
        self.lines = lines            # list of source lines
        scan_escape(lines)            # OPTION ESCAPE's positional gate
        self.srcname = srcname
        self.globals = {}             # canonical -> Sym
        self.routines = {}            # canonical -> Routine
        self.routine_names = {}       # names known before pass 1
        self.labels = {}
        self.errors = []
        self.warnings = []
        self.implied = []             # (name, ty, line, routine)
        self.data = []                # DATA items, in program order
        self.data_at = {}             # label -> index of the next DATA item
        self.lenient = True           # comment out what cannot be translated
        self.fcc = False              # C89 output for the Fuzix C compiler:
                                      # no compound literals - hoisted bounds
                                      # tables and mm_byref instead
        self.bnd_tables = {}          # array acc -> (table name, dims text)
        self.skipped = []             # (line, source text, reason)
        self.labels_used = {}         # labels that some GOTO targets
        self.label_depth = {}         # block nesting where a label sits
        self.goto_depth = {}          # shallowest nesting that jumps to it
        self.label_routine = {}       # label -> the routine it sits in
        self.gosub_sites = {}         # routine -> [(site id, label)]
        self.gosub_n = 0              # site counter, reset for each walk
        self.opt_default = TY_F
        self.opt_explicit = False
        self.opt_base = 0
        # OPTION ANGLE: MMBasic's `optionangle`, a plain multiplier
        # (MM_Misc.c:5064).  1.0 is RADIANS and emits nothing; DEGREES
        # is RADCONV and folds into the trig call sites, so a program
        # in radians - which is every program that never says - pays
        # nothing at all for this.
        self.opt_angle = None
        self.opt_angle_seen = False
        self.opt_angle_line = 0
        # per pass state
        self.mode = 'scan'
        self.cur = None               # current Routine or None
        self.out_main = []
        self.out_body = []
        self.out = None
        self.indent = 1
        self.blocks = []
        self.tmpn = 0
        self.lineno = 0
        self.toks = []
        self.i = 0
        self.tmp_used = False
        self.uses_clear = False
        self.types = {}           # canonical name -> TypeDef
        self.type_order = []      # registration order, for emission
        self.in_type = False      # inside TYPE...END TYPE in this pass
        self.uses_circle = False
        self.uses_box = False
        self.uses_gui = False
        self.uses_i2c0 = False
        self.uses_linew = False
        self.reads_string = False
        self.redimmed = {}
        self.uses_rbox = False
        self.uses_triangle = False
        self.uses_polygon = False
        self.uses_bezier = False
        self.uses_fill = False
        self.uses_arc = False
        self.uses_text = False
        # a FRAMEBUFFER buffer named by a string the program computes
        self.uses_fbsel = False
        # DefineFont blocks, number -> the font's bytes in the kernel's
        # layout.  Collected by pass_fonts before anything else runs.
        self.fonts = {}
        self.uses_mappal = False
        self.uses_gpio = False
        self.uses_pioout = False    # WS2812/BITSTREAM: mmb_pioout.h
        self.uses_port = False      # PORT: pulls in mmb_port.h
        self.uses_math = False      # MATH C_ADD etc: pulls in mmb_math.h
        self.uses_crc = False       # MATH(CRCn ...): pulls in mmb_crc.h
        self.uses_sort = False      # SORT: pulls in mmb_sort.h
        self.uses_array = False     # whole-array ops/REDIM/MATH(): mmb_array.h
        self.uses_lstring = False   # LONGSTRING: pulls in mmb_lstring.h
        self.uses_datetime = False  # DATE$/TIME$/EPOCH etc: mmb_datetime.h
        self.uses_data = False      # DATA/READ/RESTORE: mmb_data.h
        self.uses_misc = False      # GOSUB/BIT/FLAG/BIN2STR etc: mmb_misc.h
        self.uses_pulse = False     # PULSE: pulls in mmb_pulse.h
        self.uses_wait = False      # a serviced PAUSE: pulls in mmb_wait.h
        self.uses_comms = False     # I2C/SPI data forms: mmb_comms.h
        self.uses_onewire = False   # ONEWIRE/TEMPR: mmb_onewire.h
        self.uses_pulsin = False    # Pulsin(/Distance(: mmb_pulsin.h
        self.uses_net = False       # the socket floor: mmb_net.h
        self.uses_udp = False       # WEB UDP: mmb_udp.h
        self.uses_webclient = False # WEB TCP/TLS client: mmb_webc.h
        self.uses_webserver = False # WEB TCP server: mmb_webs.h
        self.uses_json = False      # JSON$: mmb_json.h
        # one entry per TRANSMIT PAGE call site: the normalised
        # expression texts, emitted as __mmwebsub_N at file scope
        self.websubs = []
        self.uses_play = False
        self.uses_blit = False      # BLIT family: mmb_blit.h
        self.uses_flash = False     # pseudo flash slots: mmb_flash.h
        self.uses_sprite = False    # SPRITE family: mmb_sprite.h
        self.uses_playd = False     # SOUND/TONE/MOD daemons: mmb_play.h
        # FRAMEBUFFER LAYER with a transparent colour: the colour is
        # run-time state (the firmware's transparentlow/high), kept in
        # an emitted global that MERGE reads when it names no colour
        self.uses_fbt = False
        # CALL by name: one dispatcher per distinct argument shape,
        # emitted after the routine bodies (see call_dispatch)
        self.call_disp = []
        self.lit_names = None       # see lit_routine_names()
        self.uses_pwm = False
        self.uses_i2c = False
        self.uses_spi = False
        self.uses_peek = False      # PEEK(): pulls in mmb_peek.h
        # set in the scan pass: any ON ERROR at all pulls in the __mm_e
        # state, the routine prologues and mm_err_bind
        self.uses_onerror = False
        # ON ERROR IGNORE arms trapping for an unbounded stretch of the
        # program, so every statement pays the checked forms - set in
        # the scan pass, statements BEFORE the line included, because
        # the armed window is a run-time thing.  A literal ON ERROR
        # SKIP n arms exactly the ON ERROR statement plus the next n:
        # the checked forms and the per-statement bookkeeping are
        # emitted for that window alone (err_window, counted down in
        # statement()), which is why one first-use guard in a big
        # program no longer costs the whole program checked arithmetic.
        # A SKIP whose count is not a bare literal is treated as IGNORE.
        self.onerror_global = False
        self.err_window = 0
        self.err_window_pending = None
        # likewise: an interrupt armed at line 100 has to be polled by
        # the statements before it, so the poll sites are emitted for
        # the whole program or none of it
        self.uses_interrupts = False
        self.uses_cmdline = False       # MM.CMDLINE$: main takes argv
        # depth of single-line IF bodies being emitted: END SUB means
        # "return now" in there, not "the routine ends here"
        self.inline = 0

    # -- error reporting ------------------------------------------------
    def err(self, msg):
        raise MMError("line %d: %s" % (self.lineno, msg))

    def note(self, msg):
        """An error that must not stop the parse: recorded exactly as
        err() would record it, but the statement is allowed to finish so
        one bad line does not cascade into twenty."""
        text = "line %d: %s" % (self.lineno, msg)
        if text not in self.errors:
            self.errors.append(text)

    def warn(self, msg):
        text = "line %d: %s" % (self.lineno, msg)
        if text not in self.warnings:
            self.warnings.append(text)

    # -- token access ---------------------------------------------------
    def peek(self, k=0):
        j = self.i + k
        if j < len(self.toks):
            return self.toks[j]
        return None

    def at_end(self):
        return self.i >= len(self.toks)

    def nxt(self):
        t = self.peek()
        if t is None:
            self.err("unexpected end of line")
        self.i += 1
        return t

    def is_op(self, s, k=0):
        t = self.peek(k)
        return t is not None and t[0] == T_OP and t[1] == s

    def checks_on(self):
        """Are the ON ERROR checked forms live for the statement being
        emitted?  IGNORE (or an unknowable SKIP count) arms the whole
        program; a literal SKIP n arms the ON ERROR statement and the
        next n.  MMBasic's own counter is dynamic - it follows execution
        into a called SUB - so a compiled ARITHMETIC error deeper in a
        callee than the routine-entry decrements reach is not trapped
        where the interpreter would have counted its way to it; a
        runtime command error is trapped anywhere, because the runtime
        consults the armed count wherever it is raised."""
        return self.onerror_global or self.err_window > 0

    def is_kw(self, s, k=0):
        t = self.peek(k)
        return t is not None and t[0] == T_ID and t[2] == s

    def accept_op(self, s):
        if self.is_op(s):
            self.i += 1
            return True
        return False

    def expect_op(self, s):
        if not self.accept_op(s):
            # "syntax error" first, because that is what it is and what
            # MMBasic calls it.  A bare "expected ')'" reads as a missing
            # bracket and sends the reader looking for one; the real
            # cause is usually an argument form this translator does not
            # take yet, with the ')' simply being where it gave up.
            self.err("syntax error (expected '%s')" % s)

    def accept_kw(self, s):
        if self.is_kw(s):
            self.i += 1
            return True
        return False

    @staticmethod
    def looks_like_just(s):
        """Does this text parse as a justification?

        mmg_just's grammar and MMBasic's GetJustification: [L|C|R] then
        [T|M|B] then [N|V|I|U|D], each of them optional."""
        u = s.upper()
        i = 0
        if i < len(u) and u[i] in 'LCR':
            i += 1
        if i < len(u) and u[i] in 'TMB':
            i += 1
        if i < len(u) and u[i] in 'NVIUD':
            i += 1
        return len(u) > 0 and i == len(u)

    def just_arg(self):
        """TEXT's justification: a bare word or a string.

        MMBasic tries the argument's RAW TEXT as a justification before
        it evaluates anything (Draw.c:2148-2149), which is what makes
        `TEXT x, y, s$, CM' work unquoted - picofrog writes it that way
        and so does most PicoMite code.  The ambiguity that comes with
        it is the reference's too: a variable called C loses to the
        justification C, there and here.

        Only when the word IS the whole argument: `C + "M"' has to be
        evaluated, and MMBasic tries its whole text first for the same
        reason."""
        t = self.peek()
        if t is not None and t[0] == T_ID and self.looks_like_just(t[1]):
            nxt = self.peek(1)
            if nxt is None or (nxt[0] == T_OP and nxt[1] in (',', ':')) \
                    or (nxt[0] == T_ID and nxt[2] == 'ELSE'):
                self.i += 1
                return c_string_literal(t[1])
        return self.as_str(self.expr())

    def kw_or_str(self, table, rt, what):
        """A bare letter, a quoted letter, or a string at run time.

        MMBasic's own two-stage form, and both stages are in its source
        for each of these: cmd_framebuffer and cmd_play try
        checkstring() against the bare token first and fall through to
        getCstring() + strcasecmp() - which is why "b" and "B" and a
        bare B all work, and why a variable is allowed where the
        manual only ever shows a letter.  picofrog needs both:
        `FRAMEBUFFER WRITE L' and `framebuffer write lc$', `PLAY SOUND
        1,"L","q",...' and `PLAY SOUND 4,B,Q,...'.

        A quoted letter is decided HERE rather than at run time: it is
        knowable now, and an unknown one is then a translation error
        instead of something the program discovers when it plays.

        `table' is [(letter, value)], `rt' the runtime decoder for the
        case that is only knowable when it runs.  Returns a C
        expression either way.
        """
        for nm, val in table:
            if self.is_kw(nm):
                self.i += 1
                return str(val)
        t = self.peek()
        if t is not None and t[0] == T_STR:
            up = t[1].strip().upper()
            for nm, val in table:
                if up == nm:
                    self.i += 1
                    return str(val)
            self.err(what)
        return '%s(%s)' % (rt, self.as_str(self.expr()))

    # N is the screen, F the off-screen buffer and L the layer - which
    # is just a second off-screen buffer, and becomes a layer only in
    # MERGE.  MMBasic's T and 2 name buffers this machine does not
    # have, so they are refused rather than quietly becoming one of
    # these three.
    FB_BUFS = (('N', 0), ('F', 1), ('L', 2))

    def fb_buf(self):
        """Which framebuffer a FRAMEBUFFER argument names, as a C
        expression - a constant when it is written as a letter, and
        __mmb_fbsel() when the program works it out as it runs."""
        e = self.kw_or_str(self.FB_BUFS, '__mmb_fbsel',
                           "expected N, F or L")
        if not e.isdigit():
            self.uses_fbsel = True
        return e

    def stmt_end(self):
        # ELSE terminates a statement too, so that the PRINT in
        #   IF a THEN PRINT "x" ELSE PRINT "y"
        # does not swallow the rest of the line.
        return self.at_end() or self.is_op(':') or self.is_kw('ELSE')

    # -- emission -------------------------------------------------------
    def emit(self, text):
        if self.mode != 'emit':
            return
        self.out.append('    ' * self.indent + text)

    def last_line(self):
        """Index of the line emit() just wrote, or None outside emission.

        For patching a call after the fact - see do_print, which turns
        the last item of a PRINT into its flushing variant."""
        if self.mode != 'emit':
            return None
        return len(self.out) - 1

    def raw(self, text):
        if self.mode != 'emit':
            return
        self.out.append(text)

    def newtmp(self, pfx):
        self.tmpn += 1
        return '__%s%d' % (pfx, self.tmpn)

    def store(self, target, val, ty):
        """A numeric assignment, guarded when the program uses ON ERROR.

        The interpreter never performs the assignment whose expression
        failed - it jumps away before the store.  Here the expression has
        already run, so the value goes to a temporary first and is
        committed only if the statement survived.  Testing the flag before
        evaluating would be too early: the flag is what evaluating sets.

        String assignment needs none of this: it goes through mm_sset,
        which checks for itself."""
        ctype = 'MMINTEGER' if ty == TY_I else 'MMFLOAT'
        if not self.checks_on():
            self.emit('%s = %s;' % (target, val))
            return
        tmp = self.newtmp('cv')
        self.emit('{ %s %s = %s;' % (ctype, tmp, val))
        self.emit('  if (!__mm_e[0]) %s = %s; }' % (target, tmp))

    # ==================================================================
    #  symbol lookup / creation - this is where implied globals appear
    # ==================================================================

    def lookup(self, canon):
        """Local (or param) first, then global.  None if unknown."""
        if self.cur is not None:
            s = self.cur.locals.get(canon)
            if s is not None:
                return s
        return self.globals.get(canon)

    def declare(self, canon, ty, scope, arr_dims=None, static=False):
        """Explicit DIM / LOCAL / STATIC / CONST / parameter."""
        if scope == 'local':
            table = self.cur.locals
        else:
            table = self.globals
        for sfx in ('', '$'):
            nm = (canon + sfx).upper()
            # only the built-ins that can appear without brackets are
            # genuinely reserved; MIN, LEN and friends are told apart from
            # a variable by the '(' that follows them
            if nm in BUILTINS and BUILTINS[nm][0] == 0:
                self.err("'%s' is a built-in function and cannot be used as "
                         "a variable name" % canon)
        old = table.get(canon)
        if old is not None:
            if old.ty != ty:
                self.err("'%s' already declared as %s"
                         % (canon, TYNAME[old.ty]))
            return old
        s = Sym(canon, ty, cvar(canon))
        s.where = self.lineno
        s.is_static = static
        s.declared_in = self.cur.name if (scope == 'local' and self.cur) else ''
        if arr_dims is not None:
            s.is_array = True
            s.dims = arr_dims
        table[canon] = s
        if scope == 'local':
            self.cur.local_order.append(canon)
        if scope == 'local' and static:
            self.cur.statics.append(s)
        return s

    def reference(self, word, as_array):
        """A name used in an expression or as an assignment target.

        If it is not already known then MMBasic would create it, right
        here, as a GLOBAL - even when we are inside a subroutine.  That
        is the implied-declaration rule, and this is the one place it
        happens."""
        canon, sfx = split_suffix(word)
        s = self.lookup(canon)
        if s is not None:
            if sfx is not None and sfx != s.ty:
                self.err("'%s' is %s but used as %s"
                         % (canon, TYNAME[s.ty], TYNAME[sfx]))
            self.note_touch(canon, s)
            return s
        # MM. is MMBasic's own namespace, not the program's: a name in
        # it is a read we have not translated, and turning it into an
        # implied variable makes it answer 0 for ever.  MM.WIDTH did
        # exactly that - it compiled clean and printed 0 where a
        # PicoMite prints 80.  The translated ones never reach here;
        # they are matched in the builtin path above.
        if canon.startswith('mm.'):
            self.err("%s is not translated" % word)
        # not known -> implied global
        if as_array:
            self.err("array '%s' used but never DIMensioned" % canon)
        ty = sfx if sfx is not None else self.opt_default
        if ty is None:
            self.err("OPTION DEFAULT NONE: '%s' has no type" % canon)
        if self.opt_explicit:
            self.err("OPTION EXPLICIT: '%s' has not been declared" % canon)
        if self.mode != 'emit':
            s = Sym(canon, ty, cvar(canon))
            s.disp = word
            s.where = self.lineno
            s.implied = True
            self.globals[canon] = s
            self.implied.append((canon, ty, self.lineno,
                                 self.cur.name if self.cur else ''))
            self.note_touch(canon, s)
            return s
        self.err("internal: '%s' unresolved in emit pass" % canon)

    def note_touch(self, canon, s):
        """Remember that this SUB/FUNCTION reached out to a global."""
        if self.mode != 'scan' or self.cur is None:
            return
        if s.is_const or s.is_param:
            return
        if canon in self.cur.locals:
            return
        if canon not in self.cur.gtouch:
            self.cur.gtouch[canon] = self.lineno

    # ==================================================================
    #  expressions
    # ==================================================================

    def as_int(self, v):
        code, ty = v
        if ty == TY_I:
            return code
        if ty == TY_F:
            return 'mm_toint(' + code + ')'
        if isinstance(ty, tuple):
            self.err("a whole structure cannot be used in an expression")
        self.err("string used where a number is required")

    def as_flt(self, v):
        code, ty = v
        if ty == TY_F:
            return code
        if ty == TY_I:
            f = float_form_of_int_literal(code)
            return f if f is not None else '(MMFLOAT)(' + code + ')'
        if isinstance(ty, tuple):
            self.err("a whole structure cannot be used in an expression")
        self.err("string used where a number is required")

    def as_str(self, v):
        # An MMBasic string - length byte, data, NUL - not a C one.  The
        # callee is expected to know that and use mm_slen/mm_cstr.
        code, ty = v
        if ty == TY_S:
            return code
        if isinstance(ty, tuple):
            self.err("a whole structure cannot be used in an expression")
        self.err("number used where a string is required")

    # -- structure member access -----------------------------------------
    #
    # A dotted identifier is ONE token (dots are name characters), so
    # p.x arrives whole and the firmware's rule applies at lookup time:
    # split at the first dot, and if the prefix names a struct variable
    # the rest is a member path.  Indices interrupt a path as separate
    # tokens - v.a(i).b is  ID('v.a') '(' i ')' '.' ID('b')  - which is
    # why the walker below alternates between parts of the current
    # token and fresh tokens fetched after ')' when a '.' follows.

    def member_path(self, base, tyname, parts, sfx):
        """Walk a member path from a struct lvalue.  Returns
        ('num', code, ty) | ('str', ptrcode, slen) |
        ('struct', code, tyname, via_member)."""
        via = False
        while True:
            if not parts:
                if self.is_op('.'):
                    self.i += 1
                    t = self.nxt()
                    if t[0] != T_ID:
                        self.err("member name expected after '.'")
                    canon, sfx = split_suffix(t[1])
                    parts = canon.split('.')
                    continue
                return ('struct', base, tyname, via)
            td = self.types[tyname]
            name = parts.pop(0)
            m = td.byname.get(name)
            if m is None:
                self.err("'%s' is not a member of TYPE '%s'"
                         % (name, td.disp))
            via = True
            code = base + '.m_' + name
            lin = None
            if m.dims is not None:
                # the index can only follow the LAST part of the token
                if not parts and self.is_op('('):
                    lin = self.member_index(m)
                elif m.stype is not None:
                    self.err("an array of nested structures needs an "
                             "index")
                else:
                    self.err("array member '%s' needs an index" % name)
            elif not parts and self.is_op('(') and m.stype is None:
                self.err("member '%s' is not an array" % name)
            if m.stype is not None:
                if lin is not None:
                    code += '[(int)(%s)]' % lin
                if parts or self.is_op('.'):
                    base = code
                    tyname = m.stype
                    continue
                return ('struct', code, m.stype, True)
            # a plain member ends the walk
            if parts:
                self.err("'%s' is not a nested structure" % name)
            if m.ty == TY_S:
                if lin is not None:
                    code = '(%s + (int)(%s) * %d)' % (code, lin,
                                                      m.slen + 1)
                if sfx is not None and sfx != TY_S:
                    self.err("member '%s' is a STRING" % name)
                return ('str', code, m.slen)
            if lin is not None:
                code += '[(int)(%s)]' % lin
            if sfx is not None and sfx != m.ty:
                self.err("member '%s' is %s" % (name, TYNAME[m.ty]))
            return ('num', code, m.ty)

    def member_index(self, m):
        """( i [, j ...] ) on a member array -> the linear index, the
        same linearisation MMBasic uses."""
        self.expect_op('(')
        idx = []
        while True:
            idx.append(self.as_int(self.expr()))
            if not self.accept_op(','):
                break
        self.expect_op(')')
        if len(idx) != len(m.dims):
            self.err("member '%s' has %d dimension(s), %d given"
                     % (m.name, len(m.dims), len(idx)))
        lin = '(%s)' % idx[0]
        mult = 1
        for k in range(1, len(idx)):
            mult *= m.dims[k - 1] + 1
            lin += ' + (%s) * %d' % (idx[k], mult)
        return lin

    def struct_head(self, word):
        """The dotted-identifier entry: split at the first dot and
        return (sym, parts, sfx) when the prefix is a struct variable,
        else None (the name stays a plain dotted variable)."""
        canon, sfx = split_suffix(word)
        if '.' not in canon:
            return None
        head = canon.split('.', 1)[0]
        s = self.lookup(head)
        if s is None or s.stype is None:
            return None
        if self.lookup(canon) is not None:
            self.err("'%s' is shadowed by struct variable '%s' - the "
                     "firmware would make it unreachable" % (canon, head))
        self.note_touch(head, s)
        return (s, canon.split('.')[1:], sfx)

    def struct_base(self, s):
        """The C lvalue for a struct variable, consuming an element
        index when it is an array."""
        if s.is_array:
            if not self.is_op('('):
                self.err("struct array '%s' needs an index here"
                         % s.name)
            return self.index(s)
        return s.acc

    def member_value(self, res):
        """member_path result -> an expression (code, ty).  A string
        member is copied to a scratch temp: member strings have no
        trailing NUL (the firmware's layout has no room for one), and
        the copy restores the invariant every consumer assumes."""
        if res[0] == 'num':
            return (res[1], res[2])
        if res[0] == 'str':
            self.tmp_used = True
            return ('mm_scopy(%s)' % res[1], TY_S)
        return (res[1], ('TM' if res[3] else 'T', res[2]))

    def need_num(self, v):
        if v[1] == TY_S:
            self.err("string used where a number is required")
        return v

    def expr(self):
        return self.e_logical()

    def e_logical(self):
        v = self.e_compare()
        while True:
            t = self.peek()
            if t is None or t[0] != T_ID or t[2] not in ('AND', 'OR', 'XOR'):
                return v
            op = t[2]
            self.i += 1
            r = self.e_compare()
            a = self.as_int(v)
            b = self.as_int(r)
            cop = {'AND': '&', 'OR': '|', 'XOR': '^'}[op]
            v = ('(' + a + ' ' + cop + ' ' + b + ')', TY_I)

    def e_compare(self):
        v = self.e_unary_not()
        while True:
            t = self.peek()
            if t is None or t[0] != T_OP:
                return v
            op = t[1]
            if op not in ('=', '<>', '<', '>', '<=', '>='):
                return v
            self.i += 1
            r = self.e_unary_not()
            cop = '==' if op == '=' else ('!=' if op == '<>' else op)
            if v[1] == TY_S or r[1] == TY_S:
                if v[1] != TY_S or r[1] != TY_S:
                    self.err("cannot compare a string with a number")
                v = ('(mm_scmp(%s, %s) %s 0)' % (v[0], r[0], cop), TY_I)
            else:
                # a C comparison is already the 1 or 0 MMBasic defines;
                # the old '? 1 : 0' was a branch diamond the compiler
                # never folded, paid on every comparison
                v = ('((%s) %s (%s))' % (v[0], cop, r[0]), TY_I)

    def e_unary_not(self):
        t = self.peek()
        if t is not None and t[0] == T_ID and t[2] in ('NOT', 'INV'):
            self.i += 1
            v = self.e_unary_not()
            if t[2] == 'NOT':
                return ('((%s) == 0)' % self.as_flt(v), TY_I)
            return ('(~(%s))' % self.as_int(v), TY_I)
        return self.e_shift()

    def e_shift(self):
        v = self.e_add()
        while self.is_op('<<') or self.is_op('>>'):
            op = self.nxt()[1]
            r = self.e_add()
            v = ('((%s) %s (%s))' % (self.as_int(v), op, self.as_int(r)), TY_I)
        return v

    def e_add(self):
        v = self.e_mul()
        while self.is_op('+') or self.is_op('-'):
            op = self.nxt()[1]
            r = self.e_mul()
            if v[1] == TY_S or r[1] == TY_S:
                if op != '+' or v[1] != TY_S or r[1] != TY_S:
                    self.err("bad string operation")
                self.tmp_used = True
                v = ('mm_scat(%s, %s)' % (v[0], r[0]), TY_S)
            elif v[1] == TY_I and r[1] == TY_I:
                v = ('((%s) %s (%s))' % (v[0], op, r[0]), TY_I)
            else:
                v = ('((%s) %s (%s))'
                     % (self.as_flt(v), op, self.as_flt(r)), TY_F)
        return v

    def e_mul(self):
        v = self.e_unary()
        while True:
            t = self.peek()
            if t is None:
                return v
            if t[0] == T_OP and t[1] in ('*', '/', '\\'):
                op = self.nxt()[1]
                r = self.e_unary()
                if op == '/':
                    # op_div checks the divisor first where a bare C '/'
                    # answers inf.  The check exists so ON ERROR can trap
                    # the error; a program with no ON ERROR has nothing
                    # to trap it with - a divide by zero there is a bug
                    # the program needs fixing either way - so only
                    # trapping programs pay the runtime call.  A literal
                    # divisor that is not zero needs no check in either
                    # world: dividing by 180, 86400 or pi is most of the
                    # division a real program does.
                    rd = self.as_flt(r)
                    if nonzero_literal(rd) or not self.checks_on():
                        v = ('((%s) / (%s))' % (self.as_flt(v), rd), TY_F)
                    else:
                        v = ('mm_fdiv(%s, %s)' % (self.as_flt(v), rd), TY_F)
                elif op == '\\':
                    v = ('mm_idiv(%s, %s)'
                         % (self.as_int(v), self.as_int(r)), TY_I)
                elif v[1] == TY_I and r[1] == TY_I:
                    v = ('((%s) * (%s))' % (v[0], r[0]), TY_I)
                else:
                    v = ('((%s) * (%s))'
                         % (self.as_flt(v), self.as_flt(r)), TY_F)
            elif t[0] == T_ID and t[2] == 'MOD':
                self.i += 1
                r = self.e_unary()
                v = ('mm_mod(%s, %s)'
                     % (self.as_int(v), self.as_int(r)), TY_I)
            else:
                return v

    def e_unary(self):
        if self.is_op('-'):
            self.i += 1
            v = self.need_num(self.e_unary())
            return ('(-(%s))' % v[0], v[1])
        if self.is_op('+'):
            self.i += 1
            return self.e_unary()
        return self.e_pow()

    def e_pow(self):
        v = self.e_primary()
        if self.is_op('^'):
            self.i += 1
            r = self.e_unary()
            return ('mm_pow(%s, %s)' % (self.as_flt(v), self.as_flt(r)), TY_F)
        return v

    def e_primary(self):
        t = self.peek()
        if t is None:
            self.err("expression expected")
        if t[0] == T_NUM:
            self.i += 1
            if t[2] == 'F':
                return (t[1] if ('.' in t[1] or 'e' in t[1] or 'E' in t[1])
                        else t[1] + '.0', TY_F)
            if t[2] == 'H':
                return (t[1], TY_I)
            return (t[1] + 'LL', TY_I)
        if t[0] == T_STR:
            self.i += 1
            return (c_string_literal(t[1]), TY_S)
        if t[0] == T_OP and t[1] == '(':
            self.i += 1
            v = self.expr()
            self.expect_op(')')
            return ('(' + v[0] + ')', v[1])
        if t[0] == T_ID:
            return self.e_name()
        self.err("unexpected '%s'" % t[1])

    # -- a name in an expression ---------------------------------------
    def e_name(self):
        t = self.nxt()
        word = t[1]
        up = t[2]
        canon, sfx = split_suffix(word)

        # the current function's own name = its return value
        if self.cur is not None and self.cur.is_func \
                and canon == self.cur.name and not self.is_op('('):
            return (self.retacc(), self.cur.ty)

        # a user defined SUB/FUNCTION always wins over a built-in of the
        # same name - the manual's own examples define Trim$(), and a
        # program written before a built-in existed must keep working
        r = self.routines.get(canon)
        if r is not None:
            if not r.is_func:
                self.err("'%s' is a SUB, not a FUNCTION" % canon)
            if up in BUILTINS:
                self.warn("'%s' is also a built-in function; the version "
                          "defined in this program is being used" % t[1])
            args = self.call_args(True)
            return self.emit_call(r, args)

        # Call(name$ [, args...]) - the function form of CALL.  A user
        # routine named CALL wins above, exactly as it would for a
        # built-in.  A literal name resolves here and now to a direct
        # call; only a run-time name needs the dispatcher.
        if up == 'CALL' and self.is_op('('):
            self.i += 1
            w2 = self.peek()
            if w2 is not None and w2[0] == T_STR:
                canon2, sfx2 = split_suffix(w2[1].lower())
                r = self.routines.get(canon2)
                if r is None or not r.is_func:
                    self.err("CALL: no FUNCTION named '%s'" % w2[1])
                if sfx2 is not None and r.ty != sfx2:
                    self.err("'%s' is %s but named as %s"
                             % (canon2, TYNAME[r.ty], TYNAME[sfx2]))
                self.i += 1
                args = []
                while self.accept_op(','):
                    args.append(self.arg_item())
                self.expect_op(')')
                return self.emit_call(r, args)
            v = self.expr()
            if v[1] != TY_S:
                self.err("CALL needs the routine name in a string")
            args = []
            while self.accept_op(','):
                args.append(self.arg_item())
            self.expect_op(')')
            d = self.call_dispatch(True, args)
            return self.emit_call_byname(d, v[0], args)

        if up == 'STRUCT' and self.is_op('('):
            return self.struct_fn()

        if up in BUILTINS and (BUILTINS[up][0] == 0 or self.is_op('(')):
            return self.call_builtin(up)

        sh = self.struct_head(word)
        if sh is not None:
            s2, parts, sfx2 = sh
            base = self.struct_base(s2)
            return self.member_value(
                self.member_path(base, s2.stype, parts, sfx2))

        as_array = self.is_op('(')
        if not as_array:
            gp = gp_pin(word, self.lookup(canon))
            if gp is not None:
                return ('%dLL' % gp, TY_I)
        s = self.reference(word, as_array)
        if s.stype is not None:
            if as_array and not s.is_array:
                self.err("'%s' is not an array" % canon)
            if s.is_array and not as_array:
                self.err("struct array '%s' used without an index"
                         % canon)
            base = self.index(s) if as_array else s.acc
            return self.member_value(
                self.member_path(base, s.stype, [], sfx))
        if as_array:
            if not s.is_array:
                self.err("'%s' is not an array" % canon)
            return (self.sread(s, self.index(s)), s.ty)
        if s.is_const:
            return (s.acc, s.ty)
        if s.is_array:
            self.err("array '%s' used without an index" % canon)
        return (s.acc, s.ty)

    def struct_fn(self):
        """STRUCT(SIZEOF t$) / STRUCT(OFFSET t$, m$) / STRUCT(TYPE
        t$, m$) - the layout is fixed at translation time, so with
        literal names these are compile-time constants.  STRUCT(FIND)
        needs a runtime search and is not translated yet."""
        self.expect_op('(')
        t = self.nxt()
        sel = t[2] if t[0] == T_ID else ''
        if sel == 'FIND':
            self.err("STRUCT(FIND ...) is not translated yet")
        if sel not in ('SIZEOF', 'OFFSET', 'TYPE'):
            self.err("unknown STRUCT( selector '%s'" % t[1])
        a = self.nxt()
        if a[0] != T_STR:
            self.err("STRUCT(%s ...) takes a literal string type name "
                     "here" % sel)
        tc = a[1].lower()
        td = self.types.get(tc)
        if td is None:
            self.err("structure type '%s' not found" % a[1])
        if sel == 'SIZEOF':
            self.expect_op(')')
            return ('%dLL' % td.total, TY_I)
        self.expect_op(',')
        b = self.nxt()
        if b[0] != T_STR:
            self.err("STRUCT(%s ...) takes a literal member name here"
                     % sel)
        m = td.byname.get(b[1].lower())
        if m is None:
            self.err("member '%s' not found in structure '%s'"
                     % (b[1], a[1]))
        self.expect_op(')')
        if sel == 'OFFSET':
            return ('%dLL' % m.offset, TY_I)
        if m.stype is not None:
            return ('0LL', TY_I)      # the firmware masks T_STRUCT out
        return ('%dLL' % {TY_F: 1, TY_S: 2, TY_I: 4}[m.ty], TY_I)

    def index(self, s):
        """Consume ( i [, j ...] ) and build the C subscript."""
        self.expect_op('(')
        parts = []
        while True:
            v = self.expr()
            parts.append('(int)(%s)' % self.as_int(v))
            if not self.accept_op(','):
                break
        self.expect_op(')')
        return self.subscript(s, parts)

    def subscript(self, s, parts):
        """The C for one element, given one index text per dimension.

        Split out of index() so that ARRAY SLICE, which has to build the
        same accessor from indices it parsed itself, cannot drift from
        the folding rule below."""
        if s.is_param or s.dynamic:
            # MMBasic gives an array parameter no rank of its own - it
            # inherits whatever was passed - so the subscripts are folded
            # into one offset using the bounds handed in alongside it.
            # An array DIMmed with a run-time bound is the same shape and
            # folds the same way, out of its own bounds table.
            #
            # The fold runs from the LEFT, j a running product of the
            # earlier bounds, because that is what findvar does
            # (MMBasic.c:4871-4878).  The bounds table itself stays in
            # source order - BOUND() reports out of it.
            b = self.bnd_acc(s)
            ix = [self.rebase(p) for p in parts]
            off = ix[0]
            j = '1'
            for k in range(1, len(ix)):
                j = '((%s) * ((%s)[%d] + 1))' % (j, b, k)
                off = '((%s) + (%s) * (%s))' % (off, ix[k], j)
            return '%s[%s]' % (s.acc, off)
        if len(parts) != len(s.dims):
            self.err("'%s' has %d dimension(s), %d given"
                     % (s.name, len(s.dims), len(parts)))
        # REVERSED, and the declaration is reversed to match, so that
        # a(i, j) lands at i + j * d1.  MMBasic stores the FIRST
        # subscript adjacent (findvar, MMBasic.c:4871-4878) and a
        # program is entitled to see that through VARADDR, so the
        # storage order is part of the language, not ours to choose.
        #
        # rebase() is what makes OPTION BASE 1 dense: element 0 of the C
        # array IS a(1), so there is no unreachable slot for a whole
        # array walk to trip over.
        return s.acc + ''.join('[' + self.rebase(p) + ']'
                               for p in reversed(parts))

    def rebase(self, part):
        """A BASIC subscript as an index from element 0.

        Under OPTION BASE 0 they are the same thing and the text is
        handed back untouched, so nothing a BASE 0 program generates
        changes by a character."""
        if not self.opt_base:
            return part
        return '((%s) - %d)' % (part, self.opt_base)

    def bnd_acc(self, s):
        """The bounds table { rank, ub1, ... } that goes with a flat
        array - an array parameter's, or a run-time DIM's own."""
        if s.dynamic:
            return s.bacc
        return '__b_' + s.name.replace('.', '__')

    def dim_sizes(self, s, rank):
        """The C size of each dimension, as text.

        For a static array these are the declaration's own bounds; for a
        flat one they come out of the bounds table, where entry k + 1 is
        dimension k's upper bound."""
        if s.is_param or s.dynamic:
            b = self.bnd_acc(s)
            return ['((%s)[%d] + 1)' % (b, k + 1) for k in range(rank)]
        return ['(%s)' % d for d in s.dims]

    def varaddr(self):
        """PEEK(VARADDR v) - the address of a variable's storage.

        MMBasic's findvar with V_EMPTY_OK | V_NOFIND_ERR: the variable
        must already exist, and a whole array written a() is legal and
        answers with element 0.

        The forms and what each is the address OF:

            v           a scalar's own storage
            s$          the LENGTH BYTE, where an MMBasic string starts
            a(i)        that element
            a()         element 0, whatever the rank

        A string and an array are already addresses in C - a char[] and
        an array both decay - so only the scalars take an '&'.  Cast
        through uintptr_t for the same reason mmb_peek.h does: the board
        is 32-bit and the gates are 64-bit, and this is the cast that is
        right on both.
        """
        t = self.nxt()
        if t[0] != T_ID:
            self.err("PEEK(VARADDR ...) needs a variable")
        canon, sfx = split_suffix(t[1])
        s = self.lookup(canon)
        if s is None:
            # V_NOFIND_ERR: MMBasic will not create one here
            self.err("'%s' has not been declared" % canon)
        if sfx is not None and sfx != s.ty:
            self.err("'%s' is %s but used as %s"
                     % (canon, TYNAME[s.ty], TYNAME[sfx]))
        if s.is_const:
            self.err("'%s' is a CONST, so it has no address" % canon)
        if self.is_op('(') and self.is_op(')', 1):
            # a() - the whole array
            self.i += 2
            if not s.is_array:
                self.err("'%s' is not an array" % canon)
            # A LENGTH array is exactly what this is usually asked
            # about - a program that walks the elements itself, at the
            # spacing it declared - so take the address directly rather
            # than through array_flat, which refuses to hand the
            # elements to a runtime that would step over them wrongly.
            if s.ty == TY_S and s.slen is not None:
                return '(MMINTEGER)(uintptr_t)(%s)' % s.acc
            ptr, cnt = self.array_flat(s)
            return '(MMINTEGER)(uintptr_t)(%s)' % ptr
        if self.is_op('('):
            if not s.is_array:
                self.err("'%s' is not an array" % canon)
            el = self.index(s)
            if s.ty == TY_S:
                return '(MMINTEGER)(uintptr_t)(%s)' % el
            return '(MMINTEGER)(uintptr_t)&(%s)' % el
        if s.is_array:
            self.err("array '%s' needs () or an index" % canon)
        if s.ty == TY_S:
            return '(MMINTEGER)(uintptr_t)(%s)' % s.acc
        return '(MMINTEGER)(uintptr_t)&(%s)' % s.acc

    def retacc(self):
        return '__ret'

    # -- argument lists -------------------------------------------------
    def call_args(self, need_parens):
        """Returns a list of items.  Each item is either None (omitted),
        or ('expr', tokenslice) captured lazily as (code, ty)."""
        args = []
        if need_parens:
            self.expect_op('(')
            if self.accept_op(')'):
                return args
        else:
            if self.stmt_end():
                return args
            if self.accept_op('('):
                # sub call written with brackets: SUB(a, b)
                if self.accept_op(')'):
                    return args
                need_parens = True
        while True:
            if self.is_op(',') or (need_parens and self.is_op(')')) \
                    or (not need_parens and self.stmt_end()):
                args.append(None)
            else:
                args.append(self.arg_item())
            if self.accept_op(','):
                continue
            break
        if need_parens:
            self.expect_op(')')
        return args

    def arg_item(self):
        """One actual argument.  Detect the bare-variable and whole-array
        forms so that they can be passed by reference."""
        t = self.peek()
        if t is not None and t[0] == T_ID and t[2] not in BUILTINS:
            canon, sfx = split_suffix(t[1])
            # INSIDE A FUNCTION, ITS OWN NAME IS A VARIABLE - the return
            # value - and MMBasic passes it by reference like any other.
            # The test below skips every name that is a routine, so this
            # one fell through to the expression path and was passed BY
            # VALUE through a temporary: the callee wrote into the
            # temporary and the function returned whatever it had before.
            #
            # It compiles and runs, which is what makes it bad.  Found in
            # Pico-Vaders, whose whole controller layer is
            #     Function twait%(...)
            #       Call ctrl$, twait%
            # so every button read came back zero and the game could not
            # be started.
            if (self.cur is not None and self.cur.is_func
                    and canon == self.cur.name):
                nxt1 = self.peek(1)
                if nxt1 is None or (nxt1[0] == T_OP
                                    and nxt1[1] in (',', ')', ':')):
                    if sfx is not None and sfx != self.cur.ty:
                        self.err("'%s' is %s but used as %s"
                                 % (canon, TYNAME[self.cur.ty], TYNAME[sfx]))
                    s = Sym(canon, self.cur.ty, self.retacc())
                    self.i += 1
                    return ('var', s, None)
            if canon not in self.routines:
                nxt1 = self.peek(1)
                # whole array:  a()
                if nxt1 is not None and nxt1[0] == T_OP and nxt1[1] == '(' \
                        and self.is_op(')', 2):
                    self.i += 3
                    s = self.reference(t[1], True)
                    return ('array', s, None)
                # bare scalar variable
                after_is_end = (nxt1 is None or
                                (nxt1[0] == T_OP and nxt1[1] in (',', ')', ':')))
                if after_is_end:
                    s = self.lookup(canon)
                    if s is None:
                        s = self.reference(t[1], False)
                    if not s.is_const and not s.is_array:
                        self.i += 1
                        return ('var', s, None)
                # one ELEMENT of an array: a(i), a(i,j)
                #
                # An element is a variable like any other and MMBasic
                # passes it by reference - findvar() on "x(k)" hands the
                # sub a pointer to that element (MMBasic.c:2230, "set
                # argvalue to point to the variable's data").  Treated as
                # an expression here it was copied into a temporary, so a
                # sub that writes to its parameter wrote into the
                # temporary and the caller's array never changed.
                #
                # brownian.bas is what found it: its whole animation is
                # "vector i, direction(i), 1, x(i), y(i)" updating x()
                # and y() through the parameters.  Every atom was drawn
                # at its starting position for ever, and nothing else
                # about the program looked wrong.
                if nxt1 is not None and nxt1[0] == T_OP and nxt1[1] == '(':
                    s = self.lookup(canon)
                    if s is not None and s.is_array and not s.is_const:
                        # Only when the ')' ENDS the argument: a(i)+1 is
                        # an expression and must stay one.  Scanned, not
                        # parsed-and-backtracked, so nothing is consumed
                        # unless this really is a bare element.
                        k, depth, tk = 1, 0, None
                        while True:
                            tk = self.peek(k)
                            if tk is None:
                                break
                            if tk[0] == T_OP and tk[1] == '(':
                                depth += 1
                            elif tk[0] == T_OP and tk[1] == ')':
                                depth -= 1
                                if depth == 0:
                                    break
                            k += 1
                        after = self.peek(k + 1) if tk is not None else None
                        if tk is not None and (
                                after is None
                                or (after[0] == T_OP
                                    and after[1] in (',', ')', ':'))):
                            self.i += 1
                            return ('elem', s, (self.index(s), s.ty))
        v = self.expr()
        return ('val', None, v)

    # -- CALL by name: execute a SUB or FUNCTION named in a string ------
    #
    # MMBasic resolves the name at run time (cmd_call / the Call()
    # function).  Compiled, the possible targets are known: every
    # routine whose parameter list matches the shape of the arguments
    # at this CALL site.  One dispatcher is emitted per distinct shape
    # (__mm_calld_N, after the routine bodies): it compares the name -
    # case-insensitively, with and without the type suffix - against
    # each candidate and forwards the arguments.  A name that matches
    # nothing is a run-time error, as in the interpreter.  Documented
    # divergence: MMBasic would also find a routine whose parameters do
    # NOT fit these arguments and fail inside it; here such a routine
    # is simply never a candidate.
    def lit_routine_names(self):
        """Canonical names that appear as string literals anywhere in
        the program - the set a run-time CALL name can plausibly draw
        from when the site's own shape is ambiguous."""
        if self.lit_names is None:
            names = set()
            for idx in range(len(self.lines)):
                try:
                    toks = tokenize(self.lines[idx], idx + 1)
                except MMError:
                    continue
                for t in toks:
                    if t[0] == T_STR:
                        names.add(split_suffix(t[1].lower())[0])
            self.lit_names = names
        return self.lit_names

    def call_arg_ty(self, a):
        if a is None:
            self.err("CALL cannot omit an argument")
        if a[0] in ('var', 'elem'):
            return a[1].ty
        if a[0] == 'val':
            return a[2][1]
        self.err("a whole array cannot be passed through CALL yet")

    def call_dispatch(self, is_func, args):
        tys = [self.call_arg_ty(a) for a in args]
        nargs = len(args)
        exact = []
        coerced = []
        for nm in self.routines:
            r = self.routines[nm]
            if bool(r.is_func) != is_func or r.params is None:
                continue
            # trailing arguments may be omitted, exactly as they may in
            # a direct call: the candidate's spare parameters take their
            # defaults in the dispatcher body
            if len(r.params) < nargs:
                continue
            if any(p.stype is not None or p.is_array for p in r.params):
                continue
            ok_exact = True
            ok_coerce = True
            for p, a, ty in zip(r.params[:nargs], args, tys):
                if p.ty == ty:
                    continue
                ok_exact = False
                if a[0] == 'val' and ty == TY_I and p.ty == TY_F:
                    continue
                ok_coerce = False
                break
            if ok_exact:
                exact.append(r)
            elif ok_coerce:
                coerced.append(r)
        cands = exact if exact else coerced
        if not cands:
            self.err("CALL: no SUB or FUNCTION takes arguments of "
                     "this shape")
        rty = cands[0].ty
        if is_func and any(r.ty != rty for r in cands):
            # An ambiguous shape (a zero-argument Call() matches every
            # function).  Narrow to the routines the program actually
            # NAMES in a string literal somewhere - the set a run-time
            # name can plausibly draw from.
            lits = self.lit_routine_names()
            narrowed = [r for r in cands if r.name in lits]
            if narrowed:
                cands = narrowed
                rty = cands[0].ty
        for r in cands:
            if is_func and r.ty != rty:
                self.err("CALL: functions matching these arguments "
                         "return different types ('%s' and '%s'); "
                         "name the target with a literal string, or "
                         "make their types uniform"
                         % (cands[0].name, r.name))
        base = cands[0]
        cands = [r for r in cands
                 if all(bool(p.byref) == bool(q.byref)
                        for p, q in zip(r.params[:nargs],
                                        base.params[:nargs]))]
        key = (is_func, rty if is_func else None,
               tuple((p.ty, bool(p.byref)) for p in base.params[:nargs]),
               tuple(sorted(r.name for r in cands)))
        for d in self.call_disp:
            if d['key'] == key:
                return d
        d = {'key': key, 'name': '__mm_calld_%d' % len(self.call_disp),
             'is_func': is_func, 'nargs': nargs,
             'rep': base, 'cands': cands}
        self.call_disp.append(d)
        return d

    def emit_call_byname(self, d, nmexpr, args):
        rep = d['rep']
        out = []
        if d['is_func'] and rep.ty == TY_S:
            self.tmp_used = True
            out.append('mm_tmp()')
        out.append(nmexpr)
        for k in range(d['nargs']):
            out.append(self.pass_arg(rep.params[k], args[k], rep))
        return ('%s(%s)' % (d['name'], ', '.join(out)),
                rep.ty if d['is_func'] else None)

    def emit_call(self, r, args):
        """Build the C call text for a user SUB or FUNCTION."""
        if len(args) > len(r.params):
            self.err("too many arguments to '%s'" % r.name)
        out = []
        if r.is_func and r.ty == TY_S:
            self.tmp_used = True
            out.append('mm_tmp()')
        k = 0
        for p in r.params:
            a = args[k] if k < len(args) else None
            k += 1
            out.append(self.pass_arg(p, a, r))
        text = r.cname + '(' + ', '.join(out) + ')'
        return (text, r.ty if r.is_func else None)

    def pass_arg(self, p, a, r):
        if p.stype is not None:
            # always by reference, exactly as the firmware passes them
            if a is None:
                self.err("a structure argument to '%s' cannot be "
                         "omitted" % r.name)
            if a[0] == 'var':
                if a[1].stype != p.stype:
                    self.err("structure type mismatch in call to '%s'"
                             % r.name)
                return '&' + a[1].acc
            if a[0] == 'elem':
                # An element of an array OF structures - structtest's
                # TEST 5.  It reaches here as its own kind now, and the
                # address of the element is what the callee wants, the
                # same as every other structure argument.
                if a[1].stype != p.stype:
                    self.err("structure type mismatch in call to '%s'"
                             % r.name)
                return '&' + a[2][0]
            if a[0] == 'val':
                code, ty = a[2]
                if not isinstance(ty, tuple) or ty[1] != p.stype:
                    self.err("structure type mismatch in call to '%s'"
                             % r.name)
                return '&' + code
            self.err("'%s' expects a structure here" % r.name)
        if a is not None and a[0] == 'var' and a[1].stype is not None:
            self.err("a structure cannot be passed to a plain "
                     "parameter of '%s'" % r.name)
        if p.is_array:
            if a is None:
                self.err("array argument to '%s' cannot be omitted" % r.name)
            if a[0] != 'array':
                self.err("'%s' expects a whole array here" % r.name)
            s = a[1]
            if s.ty != p.ty:
                self.err("array type mismatch in call to '%s'" % r.name)
            if s.is_param or s.dynamic:
                # The LIVE bounds table, not one rebuilt from the DIM:
                # a REDIM since then would make the rebuilt one wrong,
                # and a dynamic array is already the pointer-plus-table
                # shape the callee wants.
                bnd = (s.bacc if s.dynamic
                       else '__b_' + s.name.replace('.', '__'))
                base = s.acc
            else:
                body = ('%d, %s'
                        % (len(s.dims),
                           ', '.join('(%s) - 1' % d for d in s.dims)))
                if self.fcc:
                    # FCC has no compound literals; the contents are
                    # compile-time constant, so hoist one static table
                    # per array to file scope instead.
                    if s.acc not in self.bnd_tables:
                        self.bnd_tables[s.acc] = ('__bnd_%d'
                                                  % len(self.bnd_tables),
                                                  body)
                    bnd = self.bnd_tables[s.acc][0]
                else:
                    bnd = '(const MMINTEGER[]){ %s }' % body
                # flatten, so the callee can index any rank it likes
                if s.ty == TY_S:
                    self.no_length_array(s)
                    base = '(char (*)[MM_STRSZ])%s' % s.acc
                else:
                    base = '(%s *)%s' % (CTYPE[s.ty], s.acc)
            return '%s, %s' % (base, bnd)
        if p.ty == TY_S:
            if a is None:
                return 'mm_tmp()'
            if a[0] == 'var':
                if a[1].ty != TY_S:
                    self.err("type mismatch in call to '%s'" % r.name)
                if p.byref:
                    return a[1].acc
                self.tmp_used = True
                return 'mm_scopy(%s)' % a[1].acc
            if a[0] == 'array':
                self.err("unexpected array argument")
            if a[0] == 'elem':
                # A string array element is already a char[]; by
                # reference it IS the element, by value a scratch copy.
                if a[1].ty != TY_S:
                    self.err("type mismatch in call to '%s'" % r.name)
                if p.byref:
                    return a[2][0]
                self.tmp_used = True
                return 'mm_scopy(%s)' % a[2][0]
            v = a[2]
            if v[1] != TY_S:
                self.err("type mismatch in call to '%s'" % r.name)
            if v[0].startswith('"'):
                # a literal is not writable: give the callee a scratch copy
                self.tmp_used = True
                return 'mm_scopy(%s)' % v[0]
            return v[0]
        ct = CTYPE[p.ty]
        if a is None:
            val = '0'
        elif a[0] == 'var':
            if a[1].ty == p.ty and p.byref:
                return '&' + a[1].acc
            val = self.as_int(('%s' % a[1].acc, a[1].ty)) if p.ty == TY_I \
                else self.as_flt(('%s' % a[1].acc, a[1].ty))
        elif a[0] == 'array':
            self.err("unexpected array argument")
        elif a[0] == 'elem':
            # The address of the element itself, so the sub writes into
            # the caller's array - the whole point of a by-reference
            # parameter.  A type that does not match falls through to a
            # converted copy, exactly as a scalar of the wrong type does.
            if a[1].ty == p.ty and p.byref:
                return '&' + a[2][0]
            v = a[2]
            val = self.as_int(v) if p.ty == TY_I else self.as_flt(v)
        else:
            v = a[2]
            val = self.as_int(v) if p.ty == TY_I else self.as_flt(v)
        if p.byref:
            if self.fcc:
                # No compound literals in FCC: the runtime parks the value
                # in a small ring of scratch slots and returns its address.
                # The slot is scratch wound back by mm_release, so it must
                # count as a consumed temporary - the per-iteration loop
                # releases used to mask this, and removing them overflowed
                # the byref stack on the eclipse.
                self.tmp_used = True
                return 'mm_byref_%s(%s)' % ('i' if p.ty == TY_I else 'f',
                                            val)
            # A compound literal needs no scratch slot, but the release
            # this asks for is still wanted: whatever temporaries the
            # PREVIOUS statement left behind would otherwise be held for
            # the whole of the call, and in a recursive routine that is
            # every level at once.  Nine levels was the wall; the --fcc
            # path above never had it because the by-ref slot made the
            # statement ask for a release anyway.
            self.tmp_used = True
            return '(%s[]){ %s }' % (ct, val)
        return '(' + val + ')'

    # -- built-in functions ---------------------------------------------
    def call_builtin(self, up):
        if up in RAWARG:
            return self.builtin_raw(up)
        lo, hi = BUILTINS[up]
        args = []
        if self.is_op('('):
            self.i += 1
            if not self.accept_op(')'):
                while True:
                    args.append(self.expr())
                    if not self.accept_op(','):
                        break
                self.expect_op(')')
        if len(args) < lo or len(args) > hi:
            self.err("%s() takes %d..%d argument(s), %d given"
                     % (up, lo, hi, len(args)))
        if up in STRFUNCS:
            self.tmp_used = True
        return self.emit_builtin(up, args)

    def emit_builtin(self, up, args):

        def f(k):
            return self.as_flt(args[k])

        def n(k):
            return self.as_int(args[k])

        def s(k):
            if args[k][1] != TY_S:
                self.err("%s() expects a string argument" % up)
            return args[k][0]

        if up == 'ABS':
            if args[0][1] == TY_I:
                return ('(MMINTEGER)llabs((long long)(%s))' % args[0][0], TY_I)
            return ('fabs(%s)' % f(0), TY_F)
        if up == 'INT':
            if args[0][1] == TY_I:
                return (args[0][0], TY_I)
            return ('(MMINTEGER)mm_int(%s)' % f(0), TY_I)
        if up == 'FIX':
            if args[0][1] == TY_I:
                return (args[0][0], TY_I)
            return ('(MMINTEGER)mm_fix(%s)' % f(0), TY_I)
        if up == 'CINT':
            return ('mm_toint(%s)' % f(0), TY_I)
        if up == 'SGN':
            return ('mm_sgn(%s)' % f(0), TY_I)
        if up in ('SQR', 'SIN', 'COS', 'TAN', 'ATN', 'LOG', 'EXP',
                  'ASIN', 'ACOS'):
            # SQR/LOG/ASIN/ACOS carry the firmware's domain checks so
            # that ON ERROR can trap them; with no ON ERROR in the
            # program the checks have no customer and the calls go
            # straight to libm.  The rest have no checks and are always
            # direct.
            if self.checks_on():
                cf = {'SQR': 'mm_sqr', 'SIN': 'sin', 'COS': 'cos',
                      'TAN': 'tan', 'ATN': 'atan', 'LOG': 'mm_log',
                      'EXP': 'exp', 'ASIN': 'mm_asin',
                      'ACOS': 'mm_acos'}[up]
            else:
                cf = {'SQR': 'sqrt', 'SIN': 'sin', 'COS': 'cos',
                      'TAN': 'tan', 'ATN': 'atan', 'LOG': 'log',
                      'EXP': 'exp', 'ASIN': 'asin', 'ACOS': 'acos'}[up]
            # OPTION ANGLE DEGREES: the argument goes in divided
            # (fun_sin/cos/tan), the answer comes out multiplied
            # (fun_atn/asin/acos).  Nothing else in this list moves.
            if self.opt_angle and up in ('SIN', 'COS', 'TAN'):
                return ('%s((%s) / %s)' % (cf, f(0), self.opt_angle), TY_F)
            if self.opt_angle and up in ('ATN', 'ASIN', 'ACOS'):
                return ('(%s(%s) * %s)' % (cf, f(0), self.opt_angle), TY_F)
            return ('%s(%s)' % (cf, f(0)), TY_F)
        if up == 'ATAN2':
            if self.opt_angle:
                return ('(atan2(%s, %s) * %s)'
                        % (f(0), f(1), self.opt_angle), TY_F)
            return ('atan2(%s, %s)' % (f(0), f(1)), TY_F)
        if up == 'DEG':
            return ('((%s) * (180.0 / 3.14159265358979323846))' % f(0), TY_F)
        if up == 'RAD':
            return ('((%s) * (3.14159265358979323846 / 180.0))' % f(0), TY_F)
        if up == 'RND':
            return ('mm_rnd()', TY_F)
        if up == 'PI':
            return ('3.14159265358979323846', TY_F)
        if up in ('MAX', 'MIN'):
            cop = '>' if up == 'MAX' else '<'
            allint = True
            for a in args:
                if a[1] != TY_I:
                    allint = False
            ty = TY_I if allint else TY_F
            cur = args[0][0] if allint else self.as_flt(args[0])
            for k in range(1, len(args)):
                o = args[k][0] if allint else self.as_flt(args[k])
                cur = '((%s) %s (%s) ? (%s) : (%s))' % (cur, cop, o, cur, o)
            return (cur, ty)
        if up == 'BIT':
            return ('(((%s) >> (%s)) & 1LL)' % (n(0), n(1)), TY_I)
        if up == 'TEMPR':
            # TEMPR(pin [, timeout]) - the DS18B20's answer.
            # It SLEEPS while the conversion runs where MMBasic
            # spins; see mmb_onewire.h.
            self.uses_gpio = True
            self.uses_onewire = True
            return ('mmow_tempr(%s, %s)'
                    % (n(0), n(1) if len(args) > 1 else '-1'), TY_F)
        if up == 'PULSIN':
            # Pulsin(pin, polarity [, t1 [, t2]]) - the width of the
            # next pulse in microseconds, or -1 on any timeout.  A
            # missing t2 is passed as -1 rather than by repeating t1,
            # so a t1 that calls a FUNCTION is evaluated once, as the
            # reference evaluates it.  The measurement itself is the
            # kernel's edge timestamps - PLAN-pulsin.md says why a
            # busy-wait cannot do this here.
            self.uses_gpio = True
            self.uses_pulsin = True
            return ('mmg_pulsin(%s, %s, %s, %s)'
                    % (n(0), n(1),
                       n(2) if len(args) > 2 else '100000LL',
                       n(3) if len(args) > 3 else '-1LL'), TY_I)
        if up == 'DISTANCE':
            # Distance(trig [, echo]) - centimetres, -1 no echo, -2 no
            # acknowledgement.  One pin means a 3-pin device where the
            # trigger and the echo are the same wire, and -1 is how
            # that reaches the runtime.
            self.uses_gpio = True
            self.uses_pulsin = True
            return ('mmg_distance(%s, %s)'
                    % (n(0), n(1) if len(args) > 1 else '-1LL'), TY_F)
        if up == 'FLAG':
            # FLAG(n) - one scratch bit.  The assigning form is a
            # statement, so a FLAG that reaches here is a read.
            self.uses_misc = True
            return ('mm_flag_get(%s)' % n(0), TY_I)
        if up == 'LEN':
            return ('(MMINTEGER)mm_slen(%s)' % s(0), TY_I)
        if up == 'ASC':
            return ('mm_asc(%s)' % s(0), TY_I)
        if up == 'BYTE':
            self.uses_misc = True
            return ('mm_byte(%s, %s)' % (s(0), n(1)), TY_I)
        if up == 'VAL':
            return ('mm_val(%s)' % s(0), TY_F)
        if up == 'INSTR':
            if len(args) == 2:
                return ('mm_instr(1, %s, %s)' % (s(0), s(1)), TY_I)
            return ('mm_instr(%s, %s, %s)' % (n(0), s(1), s(2)), TY_I)
        if up == 'TAB':
            return ('mm_tab(%s)' % n(0), TY_S)
        if up == 'TIMER':
            return ('mm_timer()', TY_F)
        if up == 'DATE$':
            self.uses_datetime = True
            return ('mm_date_str()', TY_S)
        if up == 'TIME$':
            self.uses_datetime = True
            return ('mm_time_str()', TY_S)
        if up == 'CWD$':
            return ('mm_cwd()', TY_S)
        if up == 'INKEY$':
            # The key that has been pressed, or "" - MMBasic's INKEY$,
            # and how a graphics program watches for one without
            # stopping to wait.  Before this it was not a function at
            # all: "Inkey$" became an ordinary string variable, always
            # empty, so LOOP WHILE INKEY$="" was an exit that could
            # never be taken and the program had to be interrupted.
            return ('mm_inkey()', TY_S)
        if up == 'CHR$':
            return ('mm_chr(%s)' % n(0), TY_S)
        if up == 'LEFT$':
            return ('mm_left(%s, %s)' % (s(0), n(1)), TY_S)
        if up == 'RIGHT$':
            return ('mm_right(%s, %s)' % (s(0), n(1)), TY_S)
        if up == 'MID$':
            if len(args) == 2:
                return ('mm_mid(%s, %s, -1)' % (s(0), n(1)), TY_S)
            return ('mm_mid(%s, %s, %s)' % (s(0), n(1), n(2)), TY_S)
        if up == 'STR$':
            m = n(1) if len(args) > 1 else '0'
            nn = n(2) if len(args) > 2 else 'MM_AUTO_PRECISION'
            pad = s(3) if len(args) > 3 else '"\\001" " "'
            if args[0][1] == TY_I:
                return ('mm_str_i(%s, %s, %s, %s)'
                        % (args[0][0], m, nn, pad), TY_S)
            return ('mm_str_f(%s, %s, %s, %s)' % (f(0), m, nn, pad), TY_S)
        if up == 'FORMAT$':
            fmt = s(1) if len(args) > 1 else '"\\002" "%g"'
            return ('mm_format(%s, %s)' % (f(0), fmt), TY_S)
        if up in ('HEX$', 'OCT$', 'BIN$'):
            cf = {'HEX$': 'mm_hex', 'OCT$': 'mm_oct', 'BIN$': 'mm_bin'}[up]
            w = n(1) if len(args) > 1 else '0'
            return ('%s(%s, %s)' % (cf, n(0), w), TY_S)
        if up == 'UCASE$':
            return ('mm_ucase(%s)' % s(0), TY_S)
        if up == 'LCASE$':
            return ('mm_lcase(%s)' % s(0), TY_S)
        if up == 'LTRIM$':
            return ('mm_ltrim(%s)' % s(0), TY_S)
        if up == 'RTRIM$':
            return ('mm_rtrim(%s)' % s(0), TY_S)
        if up == 'SPACE$':
            return ('mm_space(%s)' % n(0), TY_S)
        if up == 'STRING$':
            a1 = args[1]
            ch = ('mm_asc(%s)' % a1[0]) if a1[1] == TY_S else n(1)
            return ('mm_strrep(%s, %s)' % (n(0), ch), TY_S)
        if up == 'FIELD$':
            delim = s(2) if len(args) > 2 else '"\\001" ","'
            quote = s(3) if len(args) > 3 else '"\\000" ""'
            self.uses_misc = True
            return ('mm_field(%s, %s, %s, %s)'
                    % (s(0), n(1), delim, quote), TY_S)
        if up == 'MM.SPISPEED':
            # the clock SPI OPEN actually got, which is rarely the one
            # asked for - see mmb_spi.h
            self.uses_spi = True
            return ('mmspi_speed()', TY_I)
        if up == 'MM.I2C':
            return ('mm_i2c_stat()', TY_I)
        if up == 'MM.ONEWIRE':
            # What the last ONEWIRE RESET saw - MMBasic's
            # mmOWvalue, and a flat spelling there too.
            self.uses_gpio = True
            self.uses_onewire = True
            return ('mmow_last()', TY_I)
        if up == 'POS':
            # POS - the column the next character will go in, 1
            # for the start of a line.  MMBasic's fun_pos returns
            # MMCharPos, which the runtime has been tracking all
            # along for TAB; this only gives it a name.
            return ('(MMINTEGER)mm_col()', TY_I)
        if up == 'MM.HRES':
            return ('mm_hres()', TY_I)
        if up == 'MM.VRES':
            return ('mm_vres()', TY_I)
        if up == 'MM.ERRNO':
            return ('mm_errno()', TY_I)
        if up == 'MM.ERRMSG$':
            return ('mm_errmsg()', TY_S)
        if up == 'MM.MESSAGE$':
            # the last UDP datagram - the WebMite's messagebuff.  A
            # static in mmb_udp.h, not a scratch temp, so the reader
            # costs nothing and survives mm_release.
            self.uses_udp = True
            return ('mm_udp_message()', TY_S)
        if up == 'MM.ADDRESS$':
            self.uses_udp = True
            return ('mm_udp_address()', TY_S)
        if up == 'MM.VER':
            return ('mm_ver()', TY_F)
        if up == 'MM.DEVICE$':
            return ('mm_device()', TY_S)
        # The flat spellings of four MM.INFO() answers.  MMBasic has
        # both forms and programs use both - Pico-Vaders writes
        # Mm.Info(FontHeight) where another writes MM.FONTHEIGHT - so
        # they go to the same runtime call rather than being aliases
        # only one of which works.
        if up == 'MM.FONTHEIGHT':
            return ('mm_fontheight()', TY_I)
        if up == 'MM.FONTWIDTH':
            return ('mm_fontwidth()', TY_I)
        if up == 'MM.HPOS':
            return ('mm_hpos()', TY_I)
        if up == 'MM.VPOS':
            return ('mm_vpos()', TY_I)
        if up == 'MM.CMDLINE$':
            # the only thing that needs main's arguments, so main only
            # takes them when a program asks
            self.uses_cmdline = True
            return ('mm_cmdline()', TY_S)
        if up == 'KEYDOWN':
            # KEYDOWN(n): which keys are HELD, which INKEY$ cannot say -
            # a character stream has no way to express "up and fire
            # together".  0 the count, 1..6 the codes with 1 the most
            # recent, 7 the modifiers, 8 the locks, exactly MMBasic's
            # fun_keydown.
            return ('mm_keydown(%s)' % n(0), TY_I)
        if up == 'PIXEL':
            # PIXEL(x, y) reads a pixel back AS RGB888 - the kernel
            # primitive maps the mode's own colour numbering back out,
            # so nothing here knows about depths or palettes.
            return ('mm_pixel_get(%s, %s)' % (n(0), n(1)), TY_I)
        if up == 'MAP':
            # MAP(n) - the colour entry n stands for by default, which
            # is what a program must ask for to land on that entry.
            # Unaffected by remapping, as MMBasic's fun_map is.
            self.uses_misc = True
            return ('mm_map_get(%s)' % n(0), TY_I)
        if up == 'PORT':
            # PORT(pin, nbits [, pin, nbits]...) - several pins read as
            # one integer.  Pairs, so an odd count is a syntax error
            # rather than a silently dropped argument; MMBasic makes the
            # same check with (argc & 0b11) != 0b11.
            if len(args) % 2:
                self.err('PORT takes pin, nbits pairs')
            self.uses_gpio = True
            self.uses_port = True
            # A comma sequence: the groups are written, then read.  C
            # sequences the comma operator left to right, so the table
            # is full before mmg_port_get looks at it.
            iv = [self.as_int(a) for a in args]
            return ('(%s, mmg_port_get(%d))'
                    % (', '.join('mmg_port_group(%d, %s, %s)'
                                 % (k, iv[k * 2], iv[k * 2 + 1])
                                 for k in range(len(iv) // 2)),
                       len(iv) // 2), TY_I)
        if up == 'PIN':
            # PIN(n) - a digital level, a raw ADC count, or a voltage,
            # depending on what SETPIN made the pin.  The assigning
            # form PIN(n) = v is a statement.
            #
            # FLOAT, always.  MMBasic decides this at run time (T_INT
            # for digital and ARAW, T_NBR for AIN); generated C has to
            # know when it is generated, and nothing here knows what
            # mode a pin will be in - SETPIN's pin can be an expression
            # and its mode can change.  One type has to cover both and
            # it has to be the float, because a double holds 0, 1 and
            # every 12-bit count exactly while an integer cannot hold
            # 1.6523 volts.  Nothing observable changes for a digital
            # program: PRINT PIN(2) prints "1" either way.
            self.uses_gpio = True
            return ('mmg_pin_get(%s)' % n(0), TY_F)
        if up == 'SPI':
            # SPI(x) - send one unit and return the one that came back.
            # The command forms (OPEN, WRITE, READ, CLOSE) are
            # statements; this is the function, so it is an integer.
            self.uses_spi = True
            return ('mmspi_xfer1(%s)' % n(0), TY_I)
        self.err("built-in %s() is not supported yet" % up)

    # -- built-ins whose arguments are not ordinary expressions ----------
    def builtin_raw(self, up):
        if up in STRFUNCS:
            self.tmp_used = True
        if up == 'CHOICE':
            self.expect_op('(')
            c = self.expr()
            if c[1] == TY_S:
                self.err("CHOICE() condition must be a number")
            self.expect_op(',')
            a = self.expr()
            self.expect_op(',')
            b = self.expr()
            self.expect_op(')')
            if (a[1] == TY_S) != (b[1] == TY_S):
                self.err("CHOICE() branches must be the same kind")
            if a[1] == TY_S:
                return ('((%s) != 0 ? (char *)(%s) : (char *)(%s))'
                        % (c[0], a[0], b[0]), TY_S)
            if a[1] == TY_I and b[1] == TY_I:
                return ('((%s) != 0 ? (%s) : (%s))' % (c[0], a[0], b[0]), TY_I)
            return ('((%s) != 0 ? (%s) : (%s))'
                    % (c[0], self.as_flt(a), self.as_flt(b)), TY_F)

        if up == 'BOUND':
            self.expect_op('(')
            t = self.nxt()
            if t[0] != T_ID:
                self.err("BOUND() needs an array name")
            sym = self.reference(t[1], True)
            self.expect_op('(')
            self.expect_op(')')
            dim = None
            if self.accept_op(','):
                dim = self.expr()
            self.expect_op(')')
            if not sym.is_array:
                self.err("'%s' is not an array" % sym.name)
            return (self.bound_of(sym, dim), TY_I)

        if up == 'TRIM$':
            self.expect_op('(')
            src = self.expr()
            if src[1] != TY_S:
                self.err("TRIM$() needs a string")
            mask = '"\\001" " "'
            where = "'L'"
            if self.accept_op(','):
                m = self.expr()
                if m[1] != TY_S:
                    self.err("TRIM$() mask must be a string")
                mask = m[0]
                if self.accept_op(','):
                    t = self.peek()
                    if t is not None and t[0] == T_ID \
                            and t[2] in ('L', 'R', 'B'):
                        where = "'%s'" % t[2]
                        self.i += 1
                    else:
                        w = self.expr()
                        if w[1] != TY_S:
                            self.err("TRIM$() 'where' must be L, R or B")
                        where = '(mm_slen(%s) ? %s[1] : 0)' % (w[0], w[0])
            self.expect_op(')')
            self.uses_misc = True
            return ('mm_trim(%s, %s, %s)' % (src[0], mask, where), TY_S)

        if up in ('DATETIME$', 'DAY$', 'EPOCH'):
            self.uses_datetime = True
            self.expect_op('(')
            if self.is_kw('NOW'):
                self.i += 1
                arg = 'mm_epoch_now()'
            else:
                v = self.expr()
                if v[1] == TY_S:
                    arg = 'mm_epoch_str(%s)' % v[0]
                elif up == 'DATETIME$':
                    arg = self.as_int(v)
                else:
                    self.err("%s() needs a date string or NOW" % up)
            self.expect_op(')')
            if up == 'DATETIME$':
                return ('mm_datetime(%s)' % arg, TY_S)
            if up == 'DAY$':
                return ('mm_day(%s)' % arg, TY_S)
            return ('(%s)' % arg, TY_I)

        if up in ('BIN2STR$', 'STR2BIN'):
            self.expect_op('(')
            t = self.nxt()
            if t[0] != T_ID or t[2] not in BINTYPES:
                self.err("%s() needs a type such as INT32 or DOUBLE" % up)
            tyname = t[2]
            self.expect_op(',')
            v = self.expr()
            big = '0'
            if self.accept_op(','):
                b = self.nxt()
                if b[0] != T_ID or b[2] != 'BIG':
                    self.err("%s() third argument must be BIG" % up)
                big = '1'
            self.expect_op(')')
            const = 'MM_B_' + tyname
            isflt = tyname in ('SINGLE', 'DOUBLE')
            self.uses_misc = True
            if up == 'BIN2STR$':
                if isflt:
                    return ('mm_bin2str(%s, %s, 0, %s)'
                            % (const, self.as_flt(v), big), TY_S)
                return ('mm_bin2str(%s, 0.0, %s, %s)'
                        % (const, self.as_int(v), big), TY_S)
            if v[1] != TY_S:
                self.err("STR2BIN() needs a string")
            if isflt:
                return ('mm_str2bin_f(%s, %s, %s)' % (const, v[0], big), TY_F)
            return ('mm_str2bin_i(%s, %s, %s)' % (const, v[0], big), TY_I)

        if up == 'RGB':
            self.expect_op('(')
            t = self.peek()
            if t is not None and t[0] == T_ID and t[2] in RGBNAMES \
                    and self.is_op(')', 1):
                self.i += 2
                return ('0x%06XLL' % RGBNAMES[t[2]], TY_I)
            r = self.expr()
            self.expect_op(',')
            g = self.expr()
            self.expect_op(',')
            b = self.expr()
            self.expect_op(')')
            return ('((((%s) & 0xFF) << 16) | (((%s) & 0xFF) << 8) '
                    '| ((%s) & 0xFF))'
                    % (self.as_int(r), self.as_int(g), self.as_int(b)), TY_I)

        if up == 'JSON$':
            # the streaming path-walker over a LONGSTRING document -
            # mmb_json.h, fun_json's surface
            self.expect_op('(')
            self.uses_json = True
            ptr, cells = self.lsref()
            self.expect_op(',')
            p = self.expr()
            if p[1] != TY_S:
                self.err("JSON$ needs a string path")
            self.expect_op(')')
            return ('mm_json(%s, %s, %s)' % (ptr, cells, p[0]), TY_S)

        if up in ('LLEN', 'LGETSTR$', 'LGETBYTE', 'LINSTR', 'LCOMPARE',
                  'LINPUT'):
            self.expect_op('(')
            self.uses_lstring = True
            ptr, cells = self.lsref()
            if up == 'LLEN':
                self.expect_op(')')
                return ('mm_ls_len(%s)' % ptr, TY_I)
            if up == 'LCOMPARE':
                self.expect_op(',')
                bptr, bcells = self.lsref()
                self.expect_op(')')
                return ('mm_ls_compare(%s, %s)' % (ptr, bptr), TY_I)
            self.expect_op(',')
            a = self.expr()
            if up == 'LGETBYTE':
                self.expect_op(')')
                return ('mm_ls_getbyte(%s, %s, %d)'
                        % (ptr, self.as_int(a), self.opt_base), TY_I)
            if up == 'LINSTR':
                if a[1] != TY_S:
                    self.err("LINSTR needs a normal string to search for")
                st = '1'
                if self.accept_op(','):
                    st = self.as_int(self.expr())
                self.expect_op(')')
                return ('mm_ls_instr(%s, %s, %s)' % (ptr, a[0], st), TY_I)
            self.expect_op(',')
            b = self.expr()
            self.expect_op(')')
            if up == 'LGETSTR$':
                return ('mm_ls_getstr(%s, %s, %s)'
                        % (ptr, self.as_int(a), self.as_int(b)), TY_S)
            return ('mm_ls_input(%s, %s, %s, %s)'
                    % (ptr, cells, self.as_int(a), self.as_int(b)), TY_I)

        if up in ('EOF', 'LOC', 'LOF'):
            self.expect_op('(')
            fn = self.channel()
            self.expect_op(')')
            cf = {'EOF': 'mm_eof', 'LOC': 'mm_loc', 'LOF': 'mm_lof'}[up]
            return ('%s(%s)' % (cf, fn), TY_I)

        if up == 'INPUT$':
            self.expect_op('(')
            nbr = self.expr()
            self.expect_op(',')
            fn = self.channel()
            self.expect_op(')')
            return ('mm_input_str(%s, %s)' % (self.as_int(nbr), fn), TY_S)

        if up == 'DIR$':
            self.expect_op('(')
            if self.accept_op(')'):
                # DIR$() with no arguments continues the previous search
                return ('mm_dir("\\000" "", 0, 0)', TY_S)
            spec = self.expr()
            if spec[1] != TY_S:
                self.err("DIR$() needs a file specification string")
            kind = 'MM_DIR_FILE'
            if self.accept_op(','):
                t = self.nxt()
                if t[0] != T_ID or t[2] not in ('ALL', 'DIR', 'FILE'):
                    self.err("DIR$() type must be ALL, DIR or FILE")
                kind = 'MM_DIR_' + t[2]
            self.expect_op(')')
            return ('mm_dir(%s, %s, 1)' % (spec[0], kind), TY_S)

        if up == 'PEEK':
            # PEEK(BYTE addr) and its wider relatives.  The width is a
            # bare keyword, not a string and not a comma-separated
            # argument, which is why this is parsed here - MMBasic's
            # spelling.
            self.expect_op('(')
            t = self.nxt()
            widths = {'BYTE': 'mmpk_byte', 'SHORT': 'mmpk_short',
                      'WORD': 'mmpk_word', 'INTEGER': 'mmpk_integer',
                      'FLOAT': 'mmpk_float'}
            if t[0] == T_ID and t[2] == 'VARADDR':
                # PEEK(VARADDR v) - where a variable lives.
                #
                # This one needs the SYMBOL, not an address, which is
                # why it is here and not in the header: the translator
                # is the only thing that knows where a variable is.
                # MMBasic's findvar takes V_EMPTY_OK, so a whole array
                # written a() is legal and gives element 0.
                #
                # A STRING gives the address of its LENGTH BYTE, because
                # that is where an MMBasic string starts and ours have
                # the same layout - so PEEK(BYTE addr) is the length and
                # addr+1 is the first character, exactly as on a
                # PicoMite.
                a = self.varaddr()
                self.expect_op(')')
                return (a, TY_I)
            fn = widths.get(t[2]) if t[0] == T_ID else None
            if fn is None:
                # VAR and CFUNADDR are MMBasic's and are not here: VAR
                # is a byte inside a variable rather than an address,
                # and CFUNADDR names an embedded blob a compiler has no
                # equivalent for.
                self.err("PEEK(%s ...) is not supported; translated are "
                         "BYTE, SHORT, WORD, INTEGER, FLOAT and VARADDR"
                         % t[1])
            a = self.expr()
            self.expect_op(')')
            self.uses_peek = True
            return ('%s(%s)' % (fn, self.as_int(a)),
                    TY_F if t[2] == 'FLOAT' else TY_I)

        if up == 'SPRITE':
            # SPRITE(selector, ...) - Sprite.c fun_sprite, engine in
            # mmb_sprite.h.  The letters become the reference's own t
            # codes; V and D return floats (a bearing in radians and a
            # centre distance), everything else integers.  SPRITE(B...)
            # is the bounds-analysis machinery and is not translated.
            self.uses_sprite = True
            self.uses_blit = True
            self.expect_op('(')
            if self.is_kw('ST'):
                self.i += 1
                self.expect_op(',')
                if self.is_kw('COLLISION'):
                    self.i += 1
                    self.expect_op(')')
                    return ('mms_fun_st(1, 0, 0)', TY_I)
                if self.is_kw('OBJECT'):
                    self.i += 1
                    self.expect_op(')')
                    return ('mms_fun_st(2, 0, 0)', TY_I)
                self.accept_op('#')
                n = self.as_int(self.expr())
                self.expect_op(',')
                prop = None
                for nm, code in (('X', 1), ('Y', 2), ('W', 3), ('H', 4),
                                 ('A', 5)):
                    if self.is_kw(nm):
                        self.i += 1
                        prop = code
                        break
                if prop is None:
                    self.err('SPRITE(ST, n, ...) wants X, Y, W, H or A')
                self.expect_op(')')
                return ('mms_fun_st(0, %s, %d)' % (n, prop), TY_I)
            sel = None
            for nm, code in (('W', 1), ('H', 2), ('X', 3), ('Y', 4),
                             ('L', 5), ('C', 6), ('V', 7), ('T', 8),
                             ('E', 9), ('D', 10), ('A', 11), ('N', 12),
                             ('S', 13)):
                if self.is_kw(nm):
                    self.i += 1
                    sel = code
                    break
            if sel is None:
                if self.is_kw('B'):
                    self.err('SPRITE(B ...) is not translated')
                self.err('SPRITE() wants a selector letter')
            if sel == 13:
                # SPRITE(S) - the sprite that triggered the last
                # collision interrupt.  An argument is accepted and
                # ignored, because the reference accepts and ignores
                # one: fun_sprite parses up to five arguments for every
                # selector, and the t==13 arm is "iret =
                # sprite_which_collided" without ever looking at
                # argv[2] (Sprite.c:2462).  Programs are written both
                # ways - brownian.bas says SPRITE(S, i) - and refusing
                # the second argument rejected a line MMBasic runs.
                if self.accept_op(','):
                    self.accept_op('#')
                    self.expr()
                self.expect_op(')')
                return ('mms_fun(13, 0, 0, 1)', TY_I)
            if sel == 12:
                if self.accept_op(','):
                    l = self.as_int(self.expr())
                    self.expect_op(')')
                    return ('mms_fun(12, %s, 0, 2)' % l, TY_I)
                self.expect_op(')')
                return ('mms_fun(12, 0, 0, 1)', TY_I)
            self.expect_op(',')
            self.accept_op('#')
            n = self.as_int(self.expr())
            if sel in (7, 10):
                self.expect_op(',')
                self.accept_op('#')
                m = self.as_int(self.expr())
                self.expect_op(')')
                return ('mms_fun_f(%d, %s, %s)' % (sel, n, m), TY_F)
            if self.accept_op(','):
                i3 = self.as_int(self.expr())
                self.expect_op(')')
                return ('mms_fun(%d, %s, %s, 2)' % (sel, n, i3), TY_I)
            self.expect_op(')')
            return ('mms_fun(%d, %s, 0, 1)' % (sel, n), TY_I)
        if up in ('MM.INFO', 'MM.INFO$'):
            # MMBasic overlays the two spellings onto ONE function
            # (fun_info), which decides the type from the sub-keyword
            # rather than from the '$'.  So do both here, and let the
            # table below say what each returns - a program writes
            # Mm.Info$(Drive) and Mm.Info(Drive) for the same thing.
            return self.do_mm_info()

        if up == 'MATH':
            self.expect_op('(')
            t = self.nxt()
            if t[0] == T_ID and t[2] == 'BASE64':
                # MATH(BASE64 ENCODE in$, out$): returns the length,
                # writes the string into the second argument -
                # fun_math's own odd call shape, and how retic.bas
                # writes it (the out argument being the enclosing
                # Function's result variable).  Strings only; the
                # reference also takes arrays, which nothing has
                # needed yet - refused, not diverged.
                w = self.nxt()
                if w[0] != T_ID or w[2] not in ('ENCODE', 'DECODE'):
                    self.err('MATH(BASE64 ...) wants ENCODE or DECODE')
                a = self.expr()
                if a[1] != TY_S:
                    self.err('MATH(BASE64 %s) needs a string' % w[2])
                self.expect_op(',')
                t2 = self.peek()
                if t2 is None or t2[0] != T_ID:
                    self.err('MATH(BASE64 %s) output must be a string'
                             ' variable' % w[2])
                tgt, ty, cap = self.input_target()
                if ty != TY_S:
                    self.err('MATH(BASE64 %s) output must be a string'
                             ' variable' % w[2])
                self.expect_op(')')
                self.uses_math = True
                fn = 'mmg_b64_enc' if w[2] == 'ENCODE' else 'mmg_b64_dec'
                return ('%s(%s, %s, %d)'
                        % (fn, a[0], tgt,
                           255 if cap is None else cap), TY_I)
            if t[0] == T_ID and t[2] in CRCWIDTH:
                return self.do_math_crc(t[2])
            if t[0] == T_ID and t[2] == 'CROSSING':
                # MATH(CROSSING a() [, level] [, direction] [, confirm])
                #
                # A one-dimensional number array; the three tails are
                # optional and may be written empty, as the CRC family's
                # are.  What comes back is an OFFSET from the first
                # element, not a subscript - under OPTION BASE 1 a
                # program wants a(found + 1).  That is the reference's
                # behaviour.
                a = self.arrayref()
                if a.ty == TY_S:
                    self.err("Argument 1 must be a 1D numerical array")
                args = ['0.0', '1', '1']
                for k in range(3):
                    if not self.accept_op(','):
                        break
                    if self.is_op(',') or self.is_op(')'):
                        continue        # an empty slot keeps the default
                    v = self.expr()
                    args[k] = self.as_flt(v) if k == 0 else self.as_int(v)
                self.expect_op(')')
                ptr, cnt = self.array_line(a)
                self.uses_math = True
                return ('mmg_crossing_%s(%s, %s, %s)'
                        % ('i' if a.ty == TY_I else 'f', ptr, cnt,
                           ', '.join(args)), TY_I)
            if t[0] == T_ID and t[2] == 'M_DETERMINANT':
                # MATH(M_DETERMINANT a())  - a square 2-D float array
                a = self.arrayref()
                if a.ty != TY_F:
                    self.err("Argument 1 must be a floating point array")
                self.expect_op(')')
                p = self.array_plane(a)
                self.uses_math = True
                return ('mmg_mdet(%s, %s, %s)' % (p[0], p[1], p[2]), TY_F)
            if t[0] == T_ID and t[2] in ('MAGNITUDE', 'DOTPRODUCT'):
                # MATH(MAGNITUDE a())        sqrt of the sum of squares
                # MATH(DOTPRODUCT a(), b())  sum of the products
                #
                # Float arrays both, which is parsefloatarray's own
                # restriction.  MAGNITUDE takes ANY rank (dimension
                # count 0 in the reference) and reads the array flat;
                # DOTPRODUCT is one-dimensional, so it goes through
                # array_line and counts what a program can reach.
                name = t[2]
                a = self.arrayref()
                if a.ty != TY_F:
                    self.err("Argument 1 must be a floating point array")
                self.uses_math = True
                if name == 'MAGNITUDE':
                    self.expect_op(')')
                    ptr, cnt = self.array_flat(a)
                    return ('mmg_magnitude(%s, %s)' % (ptr, cnt), TY_F)
                self.expect_op(',')
                b = self.arrayref()
                if b.ty != TY_F:
                    self.err("Argument 2 must be a floating point array")
                self.expect_op(')')
                ap, an = self.array_line(a)
                bp, bn = self.array_line(b)
                return ('mmg_dot(%s, %s, %s, %s)' % (ap, an, bp, bn), TY_F)
            if t[0] == T_ID and t[2] in MATHARRAY:
                name = t[2]
                sym = self.arrayref()
                if sym.ty == TY_S:
                    self.err("MATH(%s ...) needs a numeric array" % name)
                ptr, cnt = self.array_flat(sym)
                sfx = 'i' if sym.ty == TY_I else 'f'
                idx = 'NULL'
                if name in ('MAX', 'MIN') and self.accept_op(','):
                    iv = self.nxt()
                    if iv[0] != T_ID:
                        self.err("MATH(%s) index must be an integer variable"
                                 % name)
                    isym = self.reference(iv[1], False)
                    if isym.ty != TY_I or isym.is_array:
                        self.err("MATH(%s) index must be an integer variable"
                                 % name)
                    idx = '&' + isym.acc
                self.expect_op(')')
                fn = {'SUM': 'sum', 'MEAN': 'mean', 'SD': 'sd',
                      'MAX': 'max', 'MIN': 'min', 'MEDIAN': 'med'}[name]
                self.uses_array = True
                if name in ('MAX', 'MIN'):
                    return ('mm_st_%s_%s(%s, %s, %s)'
                            % (fn, sfx, ptr, cnt, idx), TY_F)
                return ('mm_st_%s_%s(%s, %s)' % (fn, sfx, ptr, cnt), TY_F)
            if t[0] != T_ID or t[2] not in MATHFUNCS:
                known = sorted(list(MATHFUNCS) + list(MATHARRAY)
                               + list(CRCWIDTH)
                               + ['BASE64 ENCODE', 'BASE64 DECODE',
                                  'MAGNITUDE', 'DOTPRODUCT',
                                  'M_DETERMINANT', 'CROSSING'])
                self.err("MATH(%s ...) is not supported; translated are %s"
                         % (t[1], ', '.join(known)))
            name = t[2]
            a = self.expr()
            if MATHFUNCS[name] == 2:
                self.expect_op(',')
                b = self.expr()
            self.expect_op(')')
            if name == 'ATAN3':
                return ('mm_atan3(%s, %s)'
                        % (self.as_flt(a), self.as_flt(b)), TY_F)
            cf = {'COSH': 'cosh', 'SINH': 'sinh', 'TANH': 'tanh',
                  'LOG10': 'log10'}[name]
            return ('%s(%s)' % (cf, self.as_flt(a)), TY_F)

        self.err("built-in %s() is not supported yet" % up)

    def arrayref(self, need_parens=True):
        """A whole array, written a() or (for ERASE) just a."""
        t = self.nxt()
        if t[0] != T_ID:
            self.err("an array name was expected")
        sym = self.reference(t[1], self.is_op('('))
        if self.accept_op('('):
            self.expect_op(')')
        elif need_parens:
            self.err("'%s' should be written %s()" % (t[1], t[1]))
        return sym

    def is_array_arg(self):
        """Does a whole array - written a() - start here?

        MMBasic decides PIXEL's two forms at run time, by asking whether
        the argument it was handed is an array (getargaddress reports a
        count).  Here it has to be a question about the text, because
        the two forms compile to different calls; a() is the spelling
        MMBasic's own documentation uses for a whole array."""
        t = self.peek()
        return (t is not None and t[0] == T_ID
                and self.is_op('(', 1) and self.is_op(')', 2))

    def array_flat(self, s):
        """(pointer to element 0, element count) for a whole array."""
        if not s.is_array:
            self.err("'%s' is not an array" % s.name)
        if s.is_param or s.dynamic:
            b = s.bacc if s.dynamic else '__b_%s' % s.name.replace('.', '__')
            return (s.acc, 'mm_arr_count(%s)' % b)
        cnt = '(int)(%s)' % ' * '.join('(%s)' % d for d in s.dims)
        if s.ty == TY_S:
            self.no_length_array(s)
            return ('(char (*)[MM_STRSZ])%s' % s.acc, cnt)
        return ('(%s *)%s' % (CTYPE[s.ty], s.acc), cnt)

    def count_of(self, bound):
        """The number of elements a declared upper bound asks for.

        `DIM a(3)` is four elements under OPTION BASE 0 and THREE under
        BASE 1, and MMBasic allocates exactly that many either way.  We
        do now as well: an array here holds what the program can reach
        and nothing besides, so a whole-array walk sees what MMBasic's
        does and VARADDR reports what MMBasic reports.

        It did not use to.  Element 0 was kept under BASE 1 and every
        flat walk read it - `MATH(MAX)` over an all-negative array
        answered 0, `MATH(MEAN)` divided by one too many, `READ a()`
        filled from the wrong end.  The phantom element was the cause of
        all of them."""
        if self.opt_base:
            return '(%s) - %d + 1' % (bound, self.opt_base)
        return '(%s) + 1' % bound

    def usable(self, txt):
        """A dimension's element count, given its declared C size.

        These are the same number now - the declaration allocates only
        reachable elements (see count_of) - so this is the identity.  It
        is kept as the one place that says so, and because every caller
        reads better for naming what it wants."""
        return txt

    def array_line(self, s):
        """(first element, length) of the one-dimensional array that
        ARRAY SLICE fills, or that ARRAY INSERT reads."""
        if not s.is_array:
            self.err("'%s' is not an array" % s.name)
        if not (s.is_param or s.dynamic) and len(s.dims) != 1:
            self.err("'%s' has %d dimensions, and a one-dimensional "
                     "array is wanted here" % (s.name, len(s.dims)))
        if s.ty == TY_S:
            self.no_length_array(s)
        ptr = '&' + self.subscript(s, ['(int)(%d)' % self.opt_base])
        if s.is_param or s.dynamic:
            return (ptr, self.usable('mm_arr_count(%s)' % self.bnd_acc(s)))
        return (ptr, self.usable('(%s)' % s.dims[0]))

    def array_vector(self, s, parts, blank):
        """(first element, stride, length) of one line through an array.

        `parts` is one index per dimension with None where the statement
        left a blank, and the line runs along that dimension.

        Our C arrays are declared with their dimensions REVERSED so
        that the FIRST BASIC subscript is adjacent, which is MMBasic's
        own storage order (see subscript()).  The stride is therefore
        the product of the sizes to the LEFT of the blank index, and 1
        when the blank index is first - the same running product from
        the left that MMBasic's array_slice builds off[] from, so the
        addresses agree element for element."""
        if s.ty == TY_S:
            self.no_length_array(s)
        if not (s.is_param or s.dynamic) and len(s.dims) < 2:
            self.err("'%s' has one dimension, and a slice is taken from "
                     "an array of two or more" % s.name)
        sz = self.dim_sizes(s, len(parts))
        idx = list(parts)
        idx[blank] = '(int)(%d)' % self.opt_base
        ptr = '&' + self.subscript(s, idx)
        rest = sz[:blank]
        return (ptr, ' * '.join(rest) if rest else '1',
                self.usable(sz[blank]))

    def window_var(self):
        """A scalar numeric variable for MATH WINDOW to write a range
        into.  MMBasic takes a float or an integer and answers "Invalid
        variable" for anything else; the type is known here, so the
        refusal is made now and in its own words."""
        t = self.nxt()
        if t[0] != T_ID:
            self.err("MATH WINDOW's range targets must be variables")
        sym = self.reference(t[1], False)
        if sym.is_array or sym.ty == TY_S:
            self.err("Invalid variable")
        return (sym.acc, sym.ty)

    def array_plane(self, s):
        """(first element, columns, rows, row stride) of a 2-D array.

        MMBasic's own names: cmd_math reads dims[0] as the COLUMN count
        and dims[1] as the row count, and farr2d(arr, d1, a, b) is
        arr[b * d1 + a] - so the first subscript is the column and a row
        is contiguous.  It is contiguous here too, the storage order
        being the same one.

        The stride is the FIRST dimension's declared size rather than
        the column count, and the two differ under OPTION BASE 1: our
        arrays keep an unreachable element 0 in every dimension and
        MMBasic's do not, so the rows here are one element further
        apart than the count of what is in them."""
        if not s.is_array:
            self.err("'%s' is not an array" % s.name)
        if s.is_param or s.dynamic:
            self.err("'%s' is a run-time array, and this wants one whose "
                     "shape is known when it is translated" % s.name)
        if len(s.dims) != 2:
            self.err("'%s' has %d dimension(s), and this wants a "
                     "two-dimensional array" % (s.name, len(s.dims)))
        if s.ty == TY_S:
            self.no_length_array(s)
        sz = self.dim_sizes(s, 2)
        ptr = '&' + self.subscript(s, ['(int)(%d)' % self.opt_base] * 2)
        return (ptr, self.usable(sz[0]), self.usable(sz[1]), sz[0])

    def no_length_array(self, s):
        """Refuse to hand a LENGTH array to anything that walks its
        elements.  The runtime steps at MM_STRSZ and this one does not,
        so the alternative is silently reading from between the
        elements - an honest gap where a wrong answer would do real
        damage.  VARADDR is not affected: it wants the address, which
        is the same number either way."""
        if s.ty == TY_S and s.slen is not None:
            self.err("'%s' was DIMmed with LENGTH %d, so its elements "
                     "are %d bytes apart; passing the whole array is "
                     "not translated (index it, or drop the LENGTH)"
                     % (s.disp, s.slen, s.slen + 1))

    def lsref(self):
        """A long string: an INTEGER array holding the byte count in
        element 0 and the payload from element 1 on."""
        sym = self.arrayref()
        if sym.ty != TY_I:
            self.err("'%s' is not an integer array, so it cannot hold a "
                     "long string" % sym.name)
        if len(sym.dims) != 1:
            self.err("a long string must be a one-dimensional array")
        return self.array_flat(sym)

    def channel(self):
        """A file number, with the '#' optional as it is in MMBasic."""
        self.accept_op('#')
        v = self.expr()
        if v[1] == TY_S:
            self.err("a file number must be a number")
        return self.as_int(v)

    def bound_of(self, sym, dim):
        """BOUND() resolves at compile time for a real array; an array
        parameter carries its bounds in a hidden extra argument.

        What that argument holds is the element COUNT less one, not the
        upper bound the program wrote - the two differ under OPTION BASE
        1, and holding the count is what lets mm_arr_count and the
        subscript fold stay arithmetic-free of the base.  BOUND() is
        therefore the one place that adds it back."""
        if dim is None:
            k = 1                      # "defaults to one if not specified"
            kexpr = '1'
        elif self.is_literal_number(dim):
            k = int(dim[0].replace('LL', '').replace('(', '')
                    .replace(')', '').split('.')[0])
            kexpr = str(k)
        else:
            k = None
            kexpr = self.as_int(dim)
        if sym.is_param or sym.dynamic:
            nm = (sym.bacc if sym.dynamic
                  else '__b_' + sym.name.replace('.', '__'))
            if k == 0:
                return str(self.opt_base)
            return self.unbase('(%s)[%s]' % (nm, kexpr))
        if k is None:
            self.err("BOUND() on a DIMmed array needs a constant dimension")
        if k == 0:
            return str(self.opt_base)
        if k > len(sym.dims):
            return '0'
        return self.unbase('((%s) - 1)' % sym.dims[k - 1])

    def unbase(self, txt):
        """A stored count-less-one turned back into the upper bound the
        program wrote.  Identity under OPTION BASE 0."""
        if not self.opt_base:
            return txt
        return '((%s) + %d)' % (txt, self.opt_base)


    # ==================================================================
    #  pass 1 - collect SUB/FUNCTION signatures and explicit declarations
    # ==================================================================

    def pass_routine_names(self):
        """Cheap pre-scan so that both label detection and forward calls
        know every SUB/FUNCTION name before anything else runs."""
        for idx in range(len(self.lines)):
            self.lineno = idx + 1
            try:
                toks = tokenize(self.lines[idx], self.lineno)
            except MMError:
                continue
            k = 0
            if k < len(toks) and toks[k][0] == T_NUM:
                k += 1
            if k + 1 < len(toks) and toks[k][0] == T_ID \
                    and toks[k + 1] == (T_OP, ':', ':'):
                k += 2
            if k + 1 < len(toks) and toks[k][0] == T_ID \
                    and toks[k][2] in ('SUB', 'FUNCTION') \
                    and toks[k + 1][0] == T_ID:
                self.routine_names[split_suffix(toks[k + 1][1])[0]] = 1
            # Every array a REDIM names, wherever it stands.  In MMBasic
            # all arrays are allocated at run time so any of them can be
            # re-dimensioned; here an array with constant bounds is a C
            # array with nothing to change, so being REDIMmed anywhere
            # is what makes it dynamic - and the DIM has to know that
            # before it is translated, which is why this is a pre-scan.
            for j in range(len(toks) - 1):
                if toks[j][0] == T_ID and toks[j][2] == 'REDIM':
                    n = j + 1
                    if n < len(toks) and toks[n][0] == T_ID \
                            and toks[n][2] == 'PRESERVE':
                        n += 1
                    while n + 1 < len(toks) and toks[n][0] == T_ID:
                        self.redimmed[split_suffix(toks[n][1])[0]] = 1
                        # skip to just past this array's subscripts
                        depth = 0
                        n += 1
                        while n < len(toks):
                            if toks[n][0] == T_OP and toks[n][1] == '(':
                                depth += 1
                            elif toks[n][0] == T_OP and toks[n][1] == ')':
                                depth -= 1
                                if depth == 0:
                                    n += 1
                                    break
                            n += 1
                        if n < len(toks) and toks[n][0] == T_OP \
                                and toks[n][1] == ',':
                            n += 1
                        else:
                            break
                    break

    def pass_fonts(self):
        """Collect every DefineFont ... End DefineFont before anything
        else runs, for the same reason pass_types exists: MMBasic binds
        fonts when the program is LOADED (MMBasic.c walks the CFunction
        area filling FontTable[]), so a block at the bottom of a file is
        in force at the top of it - picofrog selects font 9 at line 95
        and defines it at line 1324.

        The body is hex, not BASIC, so it is taken here at the LINE
        level and the lines are then blanked: nothing downstream should
        try to tokenise a bare `5F200808' as an expression.

        Each 8-digit group is a 32-bit LITTLE-ENDIAN word, so the bytes
        come out reversed - and then they are exactly the layout the
        kernel's own fonts use (width, height, first char, count, then
        the glyphs MSB first).  That is the whole trick: no bit
        reordering anywhere, one swap here at translation time.
        """
        i = 0
        while i < len(self.lines):
            self.lineno = i + 1
            txt = self.lines[i].strip()
            up = txt.upper()
            if not (up.startswith('DEFINEFONT') or
                    up.startswith('DEFINE FONT')):
                i += 1
                continue
            head = txt.split(None, 2 if up.startswith('DEFINE FONT') else 1)
            num = head[-1].strip() if len(head) > 1 else ''
            num = num.lstrip('#').strip()
            # a trailing comment is legal on the DefineFont line
            for c in ("'", ';'):
                if c in num:
                    num = num.split(c)[0].strip()
            try:
                # base 10, matching mmbc's strtol(num, NULL, 10): a
                # leading zero must not quietly become octal in one
                # translator and not the other
                fno = int(num, 10)
            except ValueError:
                self.errors.append("line %d: DefineFont wants a font "
                                   "number" % self.lineno)
                self.lines[i] = ''
                i += 1
                continue
            start = i
            self.lines[i] = ''
            i += 1
            words = []
            ended = False
            bad = False
            while i < len(self.lines):
                t = self.lines[i].strip()
                u = t.upper().replace(' ', '')
                self.lines[i] = ''
                i += 1
                if u.startswith('ENDDEFINEFONT'):
                    ended = True
                    break
                # comments and blank lines inside the block are ignored,
                # as they are anywhere else
                if t.startswith("'") or not t:
                    continue
                if "'" in t:
                    t = t.split("'")[0]
                for w in t.split():
                    if len(w) != 8 or \
                            any(c not in '0123456789abcdefABCDEF'
                                for c in w):
                        # say it ONCE and keep reading to the terminator:
                        # stopping here would add a bogus "no matching
                        # End DefineFont" on top of the real complaint
                        if not bad:
                            self.errors.append(
                                "line %d: DefineFont wants 8-digit hex "
                                "words, not '%s'" % (i, w))
                        bad = True
                    else:
                        words.append(int(w, 16))
            if not ended:
                self.errors.append("line %d: DefineFont %d has no matching "
                                   "End DefineFont" % (start + 1, fno))
                continue
            if bad:
                continue
            self.define_font(fno, words, start + 1)

    def define_font(self, fno, words, where):
        """One collected block: check it and keep the bytes."""
        # 1-9 are the built-in nine, shared with the console and every
        # other program, so they cannot be replaced - and saying so is
        # the point.  A silently ignored DefineFont would draw in the
        # wrong glyphs and look like a rendering bug.
        if fno < 10 or fno > 16:
            self.errors.append("line %d: DefineFont %d - user fonts are "
                               "10 to 16 (1-9 are built in)" % (where, fno))
            return
        if fno in self.fonts:
            self.errors.append("line %d: font %d is defined twice"
                               % (where, fno))
            return
        if not words:
            self.errors.append("line %d: DefineFont %d is empty"
                               % (where, fno))
            return
        data = []
        for w in words:
            data.append(w & 0xFF)
            data.append((w >> 8) & 0xFF)
            data.append((w >> 16) & 0xFF)
            data.append((w >> 24) & 0xFF)
        wid, hgt, first, count = data[0], data[1], data[2], data[3]
        if wid == 0 or hgt == 0 or count == 0:
            self.errors.append("line %d: font %d has a zero in its header "
                               "(width %d, height %d, count %d)"
                               % (where, fno, wid, hgt, count))
            return
        if (wid * hgt) % 8:
            # what makes the glyphs plain MSB-first bytes rather than a
            # bit stream - the renderer assumes it, as MMBasic's does
            self.errors.append("line %d: font %d is %dx%d - width times "
                               "height must be a multiple of 8"
                               % (where, fno, wid, hgt))
            return
        need = 4 + count * (wid * hgt // 8)
        if len(data) < need:
            self.errors.append("line %d: font %d says %d characters of "
                               "%dx%d (%d bytes) but carries %d"
                               % (where, fno, count, wid, hgt, need,
                                  len(data)))
            return
        # trailing padding a tracker may have left is dropped: the
        # header is the authority on where the font ends
        self.fonts[fno] = data[:need]

    def pass_types(self):
        """Register every TYPE ... END TYPE before anything else needs
        one - the firmware does the same in its pre-run scan, which is
        what lets a DIM textually precede its TYPE.  Nested member
        types still resolve in textual order, exactly as the firmware
        registers them."""
        self.mode = 'types'
        td = None
        for idx in range(len(self.lines)):
            self.lineno = idx + 1
            try:
                self.toks = tokenize(self.lines[idx], self.lineno)
            except MMError:
                continue
            self.i = 0
            t = self.peek()
            if t is not None and t[0] == T_NUM and t[2] == 'I':
                self.i += 1                      # line number
            while not self.at_end():
                if self.accept_op(':'):
                    continue
                try:
                    td = self.type_statement(td)
                except MMError as e:
                    self.errors.append(str(e))
                    self.skip_statement()
        if td is not None:
            self.errors.append("line %d: TYPE '%s' has no matching END "
                               "TYPE" % (td.where, td.disp))

    def type_statement(self, td):
        """One statement of the types pass.  Returns the open TypeDef,
        or None outside a block."""
        t = self.peek()
        if t is None:
            return td
        up = t[2] if t[0] == T_ID else t[1]
        if td is None:
            if up == 'TYPE' and self.peek(1) is not None \
                    and self.peek(1)[0] == T_ID \
                    and (self.peek(2) is None
                         or self.peek(2) == (T_OP, ':', ':')):
                self.i += 1
                name = self.nxt()
                canon, sfx = split_suffix(name[1])
                if sfx is not None or '.' in canon:
                    self.err("invalid TYPE name '%s'" % name[1])
                if canon in self.types:
                    self.err("TYPE '%s' already defined" % name[1])
                if len(self.types) >= 32:
                    self.err("too many structure types (32 is the "
                             "firmware's limit)")
                td = TypeDef(canon)
                td.disp = name[1]
                td.where = self.lineno
                return td
            self.skip_statement()
            return None
        # inside a block
        if up == 'END' and self.is_kw('TYPE', 1):
            self.i += 2
            if not td.members:
                self.err("TYPE '%s' has no members" % td.disp)
            td.close()
            self.types[td.name] = td
            self.type_order.append(td.name)
            return None
        if up == 'TYPE':
            self.err("nested TYPE is not allowed")
        return self.type_member(td)

    def type_member(self, td):
        """member [ (d1[,d2...]) ] AS INTEGER|INT|FLOAT|
        STRING [LENGTH n] | <earlier typename>"""
        t = self.nxt()
        if t[0] != T_ID:
            self.err("member declaration expected inside TYPE")
        canon, sfx = split_suffix(t[1])
        if sfx is not None:
            self.err("a TYPE member takes no type suffix; use AS")
        if '.' in canon:
            self.err("a TYPE member name cannot contain '.'")
        if canon in td.byname:
            # the firmware misses this check and the duplicate becomes
            # unreachable dead space - refuse it instead
            self.err("member '%s' declared twice in TYPE '%s'"
                     % (t[1], td.disp))
        if len(td.members) >= 16:
            self.err("too many members in TYPE '%s' (16 is the "
                     "firmware's limit)" % td.disp)
        m = TypeMember(canon)
        m.disp = t[1]
        if self.accept_op('('):
            m.dims = []
            while True:
                d = self.nxt()
                if d[0] != T_NUM or d[2] != 'I':
                    self.err("a member array dimension must be a "
                             "literal integer")
                m.dims.append(int(d[1]))
                if not self.accept_op(','):
                    break
            self.expect_op(')')
        if not self.accept_kw('AS'):
            self.err("invalid member definition in TYPE (missing AS)")
        w = self.nxt()
        if w[0] != T_ID:
            self.err("member type expected")
        if w[2] in ('INTEGER', 'INT'):
            m.ty = TY_I
        elif w[2] == 'FLOAT':
            m.ty = TY_F
        elif w[2] == 'STRING':
            m.ty = TY_S
            if self.accept_kw('LENGTH'):
                n = self.nxt()
                if n[0] != T_NUM or n[2] != 'I':
                    self.err("LENGTH takes a literal integer")
                ln = int(n[1])
                if ln < 1 or ln > 255:
                    self.err("LENGTH must be 1..255")
                m.slen = ln
        else:
            tc = split_suffix(w[1])[0]
            if tc not in self.types:
                self.err("unknown type '%s' in TYPE definition (a "
                         "nested type must be defined earlier in the "
                         "file)" % w[1])
            m.stype = tc
        td.add(m, self.types)
        return td

    def skip_type_block(self, up):
        """TYPE blocks are fully processed by pass_types; every later
        pass just steps over them.  Returns True when the statement was
        part of a block."""
        if self.in_type:
            if up == 'END' and self.is_kw('TYPE', 1):
                self.i += 2
                self.in_type = False
            else:
                self.skip_statement()
            return True
        if up == 'TYPE' and self.peek(1) is not None \
                and self.peek(1)[0] == T_ID \
                and (self.peek(2) is None
                     or self.peek(2) == (T_OP, ':', ':')):
            self.i += 2
            self.in_type = True
            return True
        return False

    def pass_declarations(self):
        self.mode = 'decl'
        self.in_type = False
        self.cur = None
        for idx in range(len(self.lines)):
            self.lineno = idx + 1
            try:
                self.toks = tokenize(self.lines[idx], self.lineno)
            except MMError as e:
                self.errors.append(str(e))
                continue
            self.i = 0
            self.strip_line_number()
            while not self.at_end():
                if self.accept_op(':'):
                    continue
                try:
                    self.decl_statement()
                except MMError:
                    self.skip_statement()
        self.cur = None

    def place_label(self, canon):
        if self.mode == 'decl':
            self.labels[canon] = self.lineno
            self.data_at[canon] = len(self.data)
            self.label_routine[canon] = self.cur.name if self.cur else ''
            return
        depth = 0
        for blk in self.blocks:
            if blk[0] != 'routine':
                depth += 1
        if self.mode == 'scan':
            self.label_depth[canon] = depth
            return
        if canon not in self.labels_used:
            return              # nothing jumps here, so C needs no label
        # only worth a warning when something jumps in from further out
        if depth > 0 and self.goto_depth.get(canon, depth) < depth:
            for blk in self.blocks:
                if blk[0] != 'routine':
                    kind = blk[0].upper()
                    self.warn("label '%s' sits inside %s %s block but is "
                              "jumped to from outside it; the block's set-up "
                              "will be skipped"
                              % (canon, 'an' if kind[0] in 'AEIOU' else 'a',
                                 kind))
                    break
        self.raw(clabel(canon) + ': ;')

    def strip_line_number(self):
        """A leading integer is a line number and a leading 'name:' is a
        label; both are GOTO targets.  A bare subroutine call followed by
        a colon (Counter : Counter) must not be mistaken for a label,
        which is why the routine names are collected first."""
        t = self.peek()
        if t is not None and t[0] == T_NUM and t[2] == 'I':
            self.i += 1
            self.place_label(t[1])
        t = self.peek()
        if t is not None and t[0] == T_ID and self.is_op(':', 1) \
                and t[2] not in KEYWORDS and t[2] not in BUILTINS \
                and t[2] not in BARE_STATEMENTS:
            canon = split_suffix(t[1])[0]
            if canon not in self.routines \
                    and canon not in self.routine_names:
                self.i += 2
                self.place_label(canon)

    def skip_statement(self):
        while not self.at_end() and not self.is_op(':'):
            self.i += 1

    def decl_statement(self):
        t = self.peek()
        if t is None:
            return
        up = t[2] if t[0] == T_ID else t[1]

        if self.skip_type_block(up):
            return
        if up == 'OPTION':
            self.i += 1
            self.do_option()
            return
        if up == 'SUB' or up == 'FUNCTION':
            self.i += 1
            self.decl_routine(up == 'FUNCTION')
            self.skip_statement()
            return
        if up == 'END' and (self.is_kw('SUB', 1) or self.is_kw('FUNCTION', 1)):
            self.cur = None
            self.i += 2
            return
        if up in ('DIM', 'LOCAL', 'STATIC', 'CONST'):
            self.i += 1
            self.do_declare(up)
            return
        if up == 'DATA':
            self.i += 1
            self.collect_data()
            return
        self.skip_statement()

    def collect_data(self):
        """DATA items are gathered once, in the declaration pass, so that
        RESTORE <label> can be resolved to an index and the whole table
        emitted as static C.  MMBasic keeps the raw text and converts on
        READ, so each entry carries both forms."""
        while True:
            start = self.i
            t = self.peek()
            nxt1 = self.peek(1)
            ends = (nxt1 is None
                    or (nxt1[0] == T_OP and nxt1[1] in (',', ':')))
            if t is not None and t[0] == T_STR and ends:
                self.i += 1
                self.data.append((1, '0.0', '0LL', c_string_literal(t[1])))
            elif t is not None and t[0] == T_ID and ends \
                    and t[2] not in KEYWORDS:
                self.i += 1
                self.data.append((1, '0.0', '0LL', c_string_literal(t[1])))
            else:
                v = self.expr()
                text = self.source_text(start, self.i)
                if v[1] == TY_S:
                    self.data.append((1, '0.0', '0LL', v[0]))
                elif v[1] == TY_I:
                    self.data.append((0, '0.0', v[0],
                                      c_string_literal(text)))
                else:
                    self.data.append((2, v[0], '0LL',
                                      c_string_literal(text)))
            if not self.accept_op(','):
                break

    # Does any DATA item actually need the float column?  The int one?
    # A column no item uses is eight bytes an item of nothing, and it
    # is emitted as a NULL the runtime never dereferences: READ reaches
    # a column only for an item whose kind names it.
    @property
    def data_has_f(self):
        return any(k == 2 for k, f, i, s in self.data)

    @property
    def data_has_i(self):
        return any(k == 0 for k, f, i, s in self.data)

    # The KIND column, when every item has the same kind, is one value
    # repeated once per item - four bytes each of nothing.  Returns the
    # shared kind, or None when they differ and the column is needed.
    @property
    def data_uniform_kind(self):
        if not self.data:
            return None
        k0 = self.data[0][0]
        return k0 if all(k == k0 for k, f, i, s in self.data) else None

    # The TEXT column is MMBasic's raw source text for each item, kept
    # because READ into a string gives the text of a numeric item.  It
    # is only reachable if some item IS a string, or some READ in the
    # program targets one - and a program with neither pays four bytes
    # an item plus the literals for a column nothing can look at.
    @property
    def data_has_s(self):
        return (self.reads_string
                or any(k == 1 for k, f, i, s in self.data))

    def source_text(self, a, b):
        """Rebuild the source of tokens [a, b) - the text form of a
        numeric DATA item, for when it is READ into a string."""
        out = []
        for k in range(a, b):
            t = self.toks[k]
            if t[0] == T_STR:
                out.append('"' + t[1] + '"')
            else:
                out.append(t[1])
        return ' '.join(out)

    def decl_routine(self, is_func):
        t = self.nxt()
        if t[0] != T_ID:
            self.err("SUB/FUNCTION needs a name")
        canon, sfx = split_suffix(t[1])
        if canon in self.routines:
            self.err("'%s' defined twice" % canon)
        r = Routine(canon, is_func)
        r.disp = t[1]
        r.line = self.lineno
        r.ty = sfx if sfx is not None else self.opt_default
        self.routines[canon] = r
        self.cur = r
        # parameter list
        if self.accept_op('('):
            if not self.accept_op(')'):
                while True:
                    self.decl_param(r)
                    if not self.accept_op(','):
                        break
                self.expect_op(')')
        elif not self.stmt_end() and not self.is_kw('AS'):
            while True:
                self.decl_param(r)
                if not self.accept_op(','):
                    break
        # trailing  AS <type>  for functions
        if self.accept_kw('AS'):
            w = self.peek()
            if w is not None and w[0] == T_ID \
                    and split_suffix(w[1])[0] in self.types:
                r.ty = TY_F         # keep later passes coherent
                self.err("a FUNCTION returning a TYPE is not "
                         "translated yet")
            ty = self.type_word()
            if sfx is not None and sfx != ty:
                self.err("return type conflicts with the name suffix")
            r.ty = ty

    def decl_param(self, r):
        byref = True
        if self.accept_kw('BYVAL'):
            byref = False
        elif self.accept_kw('BYREF'):
            byref = True
        t = self.nxt()
        if t[0] != T_ID:
            self.err("bad parameter")
        canon, sfx = split_suffix(t[1])
        dims = None
        if self.accept_op('('):
            nd = 1
            while self.accept_op(','):
                nd += 1
            self.expect_op(')')
            dims = ['0'] * nd       # a rank hint only: the real rank comes
                                    # from the array the caller passes
        ty = sfx
        stype = None
        if self.accept_kw('AS'):
            w = self.peek()
            if w is not None and w[0] == T_ID \
                    and w[2] not in ('INTEGER', 'FLOAT', 'STRING') \
                    and split_suffix(w[1])[0] in self.types:
                self.i += 1
                stype = split_suffix(w[1])[0]
                if sfx is not None:
                    self.err("parameter type conflict")
                if dims is not None:
                    self.err("whole arrays of structures as parameters "
                             "are not translated yet")
                ty = TY_I
            else:
                ty2 = self.type_word()
                if ty is not None and ty != ty2:
                    self.err("parameter type conflict")
                ty = ty2
        if ty is None:
            ty = self.opt_default
        s = Sym(canon, ty, '')
        s.stype = stype
        s.is_param = True
        s.byref = byref
        s.where = self.lineno
        s.declared_in = r.name
        if stype is not None:
            # a struct parameter is always by reference, as the
            # firmware has it - BYVAL is ignored for structs there too
            s.byref = True
            s.acc = '(*p_%s)' % canon.replace('.', '__')
        elif dims is not None:
            s.is_array = True
            s.dims = dims
            s.acc = 'p_' + canon.replace('.', '__')
        elif ty == TY_S:
            s.acc = 'p_' + canon.replace('.', '__')
        elif byref:
            s.acc = '(*p_%s)' % canon.replace('.', '__')
        else:
            s.acc = 'p_' + canon.replace('.', '__')
        r.params.append(s)
        r.locals[canon] = s
        r.local_order.append(canon)

    def type_word(self):
        t = self.nxt()
        if t[0] != T_ID:
            self.err("type expected")
        if t[2] == 'INTEGER':
            return TY_I
        if t[2] == 'FLOAT':
            return TY_F
        if t[2] == 'STRING':
            return TY_S
        self.err("unknown type '%s'" % t[1])

    # MM.INFO() sub-keywords that take no argument: the word, the C call
    # and the type.  Two-word names are matched first, longest first, so
    # EXISTS DIR is never read as EXISTS.  MMBasic's own list is dozens
    # long and nearly all of it is about hardware, a flash program store
    # or a network this machine does not have; what is here is what a
    # program running on a PC3 can actually use an answer to.
    MMINFO_PLAIN = {
        'FLAGS':      ('mm_flags_get()', TY_I),
        'FONTHEIGHT': ('mm_fontheight()', TY_I),
        'FONTWIDTH':  ('mm_fontwidth()', TY_I),
        'HPOS':       ('mm_hpos()', TY_I),
        'VPOS':       ('mm_vpos()', TY_I),
        'DEVICE':     ('mm_device()', TY_S),
        'PLATFORM':   ('mm_platform()', TY_S),
        'PATH':       ('mm_path()', TY_S),
        'CURRENT':    ('mm_current()', TY_S),
        'DRIVE':      ('mm_drive()', TY_S),
        'VERSION':    ('mm_ver()', TY_F),
        'ERRNO':      ('mm_errno()', TY_I),
        'ERRMSG':     ('mm_errmsg()', TY_S),
        # the reference is time_us_64()/1000000.0 as a FLOAT
        # (MM_Misc.c fun_info UPTIME); mm_us() is the same 64-bit
        # microsecond clock - the kernel's on the board, so seconds
        # since boot, which is what the WebMite's index.html shows
        'UPTIME':     ('((MMFLOAT)mm_us() / 1000000.0)', TY_F),
        # the current drive's capacity in bytes, via statvfs
        'DISK SIZE':  ('mm_disksize()', TY_I),
    }
    # ... and the ones that take an expression after the keyword.
    MMINFO_ARG = {
        'PINNO':       ('mm_pinno(%s)', TY_S, TY_I),
        'FILESIZE':    ('mm_filesize(%s)', TY_S, TY_I),
        'EXISTS FILE': ('mm_exists_file(%s)', TY_S, TY_I),
        'EXISTS DIR':  ('mm_exists_dir(%s)', TY_S, TY_I),
    }

    def do_math_crc(self, name):
        """MATH(CRC8|CRC12|CRC16|CRC32 v [,length [,polynomial
        [,startmask [,endmask [,reverseIn [,reverseOut]]]]]]).

        Any of the six may be written EMPTY to take its default -
        `MATH(CRC16 a(), , , &HFFFF)` - which is what MMBasic's
        `if (argc > 3 && *argv[4])` amounts to (MATHS.c:3128-3138), and
        programs do write it.  The defaults are emitted as literals, so
        a program that passes none of them pays nothing for them.

        The engine and the three places it deliberately differs from
        PicoMite 6.03.00 are in mmb_crc.h.
        """
        bits, poly = CRCWIDTH[name]
        if self.is_array_arg():
            sym = self.arrayref()
            if sym.ty == TY_S:
                self.err('MATH(%s ...) wants a number array or a string'
                         % name)
            ptr, cnt = self.array_flat(sym)
            fn = 'mmg_crc_i' if sym.ty == TY_I else 'mmg_crc_f'
            src = '%s, %s' % (ptr, cnt)
        else:
            a = self.expr()
            if a[1] != TY_S:
                self.err('MATH(%s ...) wants a number array or a string'
                         % name)
            fn = 'mmg_crc_s'
            src = a[0]
        args = ['0', poly, '0', '0', '0', '0']
        for k in range(6):
            if not self.accept_op(','):
                break
            if self.is_op(',') or self.is_op(')'):
                continue                # an empty slot keeps the default
            args[k] = self.as_int(self.expr())
        self.expect_op(')')
        self.uses_crc = True
        return ('%s(%d, %s, %s)'
                % (fn, bits, src, ', '.join(args)), TY_I)

    def do_mm_info(self):
        """MM.INFO(...) and MM.INFO$(...), which are one function."""
        self.expect_op('(')
        t = self.nxt()
        if t[0] != T_ID:
            self.err("MM.INFO wants a keyword, not %s" % t[1])
        one = t[2]
        two = None
        nx = self.peek()
        if nx is not None and nx[0] == T_ID:
            two = one + ' ' + nx[2]

        # OPTION BASE is answered HERE, at translation time: OPTION BASE
        # is a compile-time setting for this translator, so the value is
        # already known and a run-time call could only look it up again.
        # It is also what unblocks the shape the ctrl library opens with,
        # Dim ctrl.key_map%(31 + Mm.Info(Option Base)).
        if two == 'OPTION BASE':
            self.i += 1
            self.expect_op(')')
            return ('%dLL' % self.opt_base, TY_I)

        # The network answers.  IP ADDRESS asks the kernel
        # (NETIOC_STATUS) and is "0.0.0.0" when there is no join - the
        # WebMite's own idle answer, which retic.bas polls for at
        # startup.  MAX CONNECTIONS is the slot count, 8 on both.
        if two == 'IP ADDRESS':
            self.i += 1
            self.expect_op(')')
            self.uses_net = True
            return ('mmn_ipaddr()', TY_S)
        if two == 'MAX CONNECTIONS':
            self.i += 1
            self.expect_op(')')
            return ('8LL', TY_I)

        # Two words first, so EXISTS DIR is never read as EXISTS.
        for words, key in ((2, two), (1, one)):
            if key is None:
                continue
            if key not in self.MMINFO_ARG and key not in self.MMINFO_PLAIN:
                continue
            if words == 2:
                self.i += 1             # the second keyword
            if key in self.MMINFO_ARG:
                fmt, want, rty = self.MMINFO_ARG[key]
                v = self.expr()
                if key == 'PINNO' and v[1] == TY_I:
                    # MM.INFO(PINNO GP1), unquoted.  MMBasic takes both:
                    # fun_info's PINNO checks the raw text for "GPnn"
                    # before evaluating it, so a bare pin name is legal
                    # there as well as a string.  Here a bare GP1 has
                    # already become the integer 1 - the name IS the
                    # number on this machine - so the answer is the
                    # value, and only the string form needs parsing.
                    self.expect_op(')')
                    return (v[0], TY_I)
                if v[1] != want:
                    self.err("MM.INFO(%s ...) wants a string" % key)
                self.expect_op(')')
                return (fmt % v[0], rty)
            if key in ('PATH', 'CURRENT'):
                # Both are argv[0], which main only receives when a
                # program asks for something that needs it - the same
                # flag MM.CMDLINE$ raises.
                self.uses_cmdline = True
            if key == 'FLAGS':
                # mm_flags_get lives in mmb_misc.h with the rest of
                # the FLAG family.
                self.uses_misc = True
            self.expect_op(')')
            return self.MMINFO_PLAIN[key]

        if one == 'FLASH':
            # MM.INFO(FLASH ADDRESS n) - the slot's base address, which
            # is how a program hands slot data to BLIT MEMORY.  The
            # pseudo slot allocates on this very reference
            # (mmb_flash.h), so asking for the address is enough.
            t = self.nxt()
            if t[0] != T_ID or t[2] != 'ADDRESS':
                self.err("MM.INFO(FLASH %s ...) is not supported; "
                         "translated is FLASH ADDRESS n" % t[1])
            a = self.expr()
            self.expect_op(')')
            self.uses_flash = True
            return ('(MMINTEGER)(long)mmf_addr(%s)' % self.as_int(a), TY_I)
        if one == 'FONT':
            # MM.INFO(FONT ADDRESS n) - where font n's glyphs are, so a
            # program can draw them itself.
            t = self.nxt()
            if t[0] != T_ID or t[2] != 'ADDRESS':
                self.err("MM.INFO(FONT %s ...) is not supported; "
                         "translated is FONT ADDRESS n" % t[1])
            a = self.expr()
            self.expect_op(')')
            return ('mm_fontaddr(%s)' % self.as_int(a), TY_I)
        self.err("MM.INFO(%s ...) is not supported; translated are "
                 "DEVICE, PLATFORM, PATH, CURRENT, DRIVE, VERSION, "
                 "ERRNO, ERRMSG, FLAGS, FONTHEIGHT, FONTWIDTH, HPOS, "
                 "VPOS, OPTION BASE, PINNO, FILESIZE, EXISTS FILE, "
                 "EXISTS DIR, FONT ADDRESS n and FLASH ADDRESS n" % t[1])

    # OPTION sub-keywords that configure a PicoMite rather than a
    # program.  What they set does not exist on this machine (a panel,
    # a keyboard layout, the editor, the flash-saved defaults), so a
    # translated program computes and prints exactly the same with them
    # or without them.  They are IGNORED, and warned about - never
    # silently dropped.  Matched on the first word, which is enough:
    # taken from the reference's own cmd_option (MM_Misc.c:4925-7722).
    # Anything not here and not implemented above is refused BY NAME,
    # because the alternative is what OPTION ANGLE used to do - change
    # the answers and say nothing.
    OPT_CONFIG = (
        'AUDIO', 'AUTORUN', 'BACKLIGHT', 'BAUDRATE', 'CACHE', 'CASE',
        'CDC', 'COLORCODE', 'COLOURCODE', 'CONTINUATION', 'CPUSPEED',
        'DISK', 'DISPLAY', 'F1', 'F5', 'F6', 'F7', 'F8', 'F9', 'FAST',
        'GPS', 'GUI', 'HDMI', 'HEARTBEAT', 'KEYBOARD', 'LCD', 'LCD320',
        'LCDPANEL', 'LIST', 'LOCAL', 'LOGGING', 'MODBUFF', 'MOUSE',
        'PICO', 'PIN', 'PLATFORM', 'POWER', 'PROFILING', 'PS2',
        'PSRAM', 'RESET', 'RTC', 'SCREEN', 'SDCARD', 'SERIAL',
        'SYSTEM', 'TELNET', 'TFTP', 'TOUCH', 'TRACECACHE', 'VCC',
        'VGA', 'WEB', 'WIFI',
    )

    def do_option(self):
        t = self.peek()
        if t is None:
            return
        if t[2] == 'DEFAULT':
            # ... but only the four type words.  OPTION DEFAULT MODE and
            # OPTION DEFAULT COLOURS are different statements that begin
            # with the same word, and falling through here is how they
            # used to vanish.
            if self.is_kw('INTEGER', 1) or self.is_kw('FLOAT', 1) or \
                    self.is_kw('STRING', 1) or self.is_kw('NONE', 1):
                self.i += 1
                w = self.nxt()
                if w[2] == 'INTEGER':
                    self.opt_default = TY_I
                elif w[2] == 'FLOAT':
                    self.opt_default = TY_F
                elif w[2] == 'STRING':
                    self.opt_default = TY_S
                elif w[2] == 'NONE':
                    self.opt_default = None
                return
        if t[2] == 'ANGLE':
            # OPTION ANGLE DEGREES | RADIANS, exactly MMBasic's
            # optionangle (MM_Misc.c:5064-5074): SIN/COS/TAN divide
            # their argument by it and ATN/ATAN2/ASIN/ACOS multiply
            # their result by it.  DEG( and RAD( are untouched.
            #
            # We fold it, because our output is compiled: it is decided
            # for the whole program in the decl pass and costs nothing
            # at all in radians.  The price of folding is that it cannot
            # follow a program that changes its mind, so a second
            # setting and a setting inside a routine are both refused
            # rather than quietly applied to the wrong half.
            # Each refusal here is FATAL rather than a commented-out
            # line, because commenting this statement out is the bug:
            # the program would translate, run, and be wrong.
            self.i += 1
            w = self.nxt()
            if w[2] == 'DEGREES':
                v = RADCONV
            elif w[2] == 'RADIANS':
                v = None
            else:
                self.note('OPTION ANGLE wants DEGREES or RADIANS')
                self.skip_statement()
                return
            if self.cur is not None:
                self.note('OPTION ANGLE is a whole-program setting and '
                          'cannot be inside a SUB or FUNCTION')
                return
            if self.opt_angle_seen and self.opt_angle != v and \
                    self.opt_angle_line != self.lineno:
                self.note('OPTION ANGLE cannot change within a program '
                          '(already set on line %d)' % self.opt_angle_line)
                return
            self.opt_angle = v
            self.opt_angle_seen = True
            self.opt_angle_line = self.lineno
            return
        if t[2] == 'EXPLICIT':
            self.i += 1
            self.opt_explicit = True
            if self.is_kw('OFF'):
                self.opt_explicit = False
                self.i += 1
            return
        if t[2] == 'BASE':
            self.i += 1
            w = self.nxt()
            self.opt_base = int(w[1])
            return
        if t[2] == 'CONSOLE':
            # OPTION CONSOLE SERIAL | SCREEN | BOTH | NONE
            #
            # A bitmask, exactly the reference's (MM_Misc.c:5178):
            # BOTH 3, SERIAL 1, SCREEN 2, NONE 0, and putConsole is
            # "if (OptionConsole & 2) DisplayPutC; if (OptionConsole & 1)
            # SerialConsolePutC" (PicoMite.c:1174).
            #
            # It is a run-time statement, not a compile-time setting: a
            # program turns the screen off round a section and back on
            # after, so it emits a call where it stands.
            #
            # This is the debugging tool that was missing.  A program in
            # a graphics mode drew its PRINTs on the screen and nothing
            # reached the console, so a trace either scrolled away under
            # the picture or was overwritten by it - and if the machine
            # then stopped, there was nothing to read anywhere.  Hours
            # went into that on picofrog before /dev/tty was used by
            # hand instead.
            self.i += 1
            w = self.nxt()
            mode = {'SERIAL': 1, 'SCREEN': 2, 'BOTH': 3, 'NONE': 0}.get(
                w[2] if w[0] == T_ID else '')
            if mode is None:
                self.err('OPTION CONSOLE wants SERIAL, SCREEN, BOTH '
                         'or NONE')
            self.emit('mm_console(%d);' % mode)
            return
        if t[2] == 'ESCAPE':
            # The decoding already happened, in the tokenizer, and
            # positionally - see scan_escape().  The statement itself
            # emits nothing; recognising it here just keeps it out of
            # skip_statement's silent bin.
            self.i += 1
            return
        if t[2] == 'UDP' and self.is_kw('SERVER', 1) and \
                self.is_kw('PORT', 2):
            # The WebMite's saved option, as the same statement WEB UDP
            # SERVER PORT emits - PLAN-web.md §3.2, so a WebMite
            # program's own OPTION line keeps working.
            self.i += 3
            self.uses_udp = True
            self.emit('mmg_udp_port(%s);' % self.as_int(self.expr()))
            return
        if t[2] == 'TCP' and self.is_kw('SERVER', 1) and \
                self.is_kw('PORT', 2):
            # likewise for the TCP server's saved option
            self.i += 3
            self.uses_webserver = True
            self.emit('mmg_webs_port(%s);' % self.as_int(self.expr()))
            return
        name = t[2] if t[0] == T_ID else str(t[1])
        if name in self.OPT_CONFIG:
            self.warn('OPTION %s configures a PicoMite; it has no effect '
                      'on a translated program and is ignored' % name)
            self.skip_statement()
            return
        # The three heads whose other forms ARE translated get their
        # second word in the message, because OPTION DEFAULT INTEGER is
        # translated and OPTION DEFAULT MODE is not, and "OPTION
        # DEFAULT" would name both.  Everywhere else the first word is
        # the whole statement's name and the rest is its arguments.
        w = self.peek(1)
        if name in ('DEFAULT', 'UDP', 'TCP') and w is not None and \
                w[0] == T_ID:
            name = '%s %s' % (name, w[2])
        # Fatal, not commented out: an OPTION changes how the REST of
        # the program behaves, so dropping the line leaves a program
        # that translates and then answers differently.
        self.note('OPTION %s is not translated' % name)
        self.skip_statement()

    # -- DIM / LOCAL / STATIC / CONST ------------------------------------
    def do_declare(self, kw):
        """Runs in every pass.  In the 'decl' pass it records the symbols;
        in the 'emit' pass it produces the initialisation code (the
        declarations themselves are hoisted to the top of the C scope)."""
        if kw == 'CONST':
            self.do_const()
            return
        scope = 'global' if kw == 'DIM' else 'local'
        if scope == 'local' and self.cur is None:
            self.err("%s is only valid inside a SUB or FUNCTION" % kw)
        static = (kw == 'STATIC')

        # optional leading type applying to the whole list
        group_ty = None
        t = self.peek()
        if t is not None and t[0] == T_ID and t[2] in ('INTEGER', 'FLOAT',
                                                       'STRING'):
            group_ty = self.type_word()

        while True:
            t = self.nxt()
            if t[0] != T_ID:
                self.err("variable name expected in %s" % kw)
            canon, sfx = split_suffix(t[1])
            dims = None
            dyn = False
            if self.accept_op('('):
                dims = []
                while True:
                    v = self.expr()
                    b = self.as_int(v)
                    # Only the declaration pass matters: it is the one
                    # that captures the bounds, and the one where a
                    # CONST still carries its literal text (by the emit
                    # pass it has become the #define's name).
                    #
                    # A bound that is not a compile-time constant makes
                    # the whole array DYNAMIC: it cannot be a C array,
                    # because the bounds would have to be in its type,
                    # so it becomes a flat pointer plus a bounds table -
                    # which is what an array parameter already is here.
                    if not const_c_expr(b):
                        dyn = True
                    dims.append(self.count_of(b))
                    if not self.accept_op(','):
                        break
                self.expect_op(')')
            ty = sfx if sfx is not None else group_ty
            stype = None
            if self.accept_kw('AS'):
                w = self.peek()
                if w is not None and w[0] == T_ID \
                        and w[2] not in ('INTEGER', 'FLOAT', 'STRING') \
                        and split_suffix(w[1])[0] in self.types:
                    self.i += 1
                    stype = split_suffix(w[1])[0]
                    if sfx is not None:
                        self.err("'%s' has a type suffix but is "
                                 "declared AS a TYPE" % canon)
                    if static:
                        self.err("STATIC of a TYPE is not translated "
                                 "yet; use DIM or LOCAL")
                else:
                    ty2 = self.type_word()
                    if ty is not None and ty != ty2:
                        self.err("conflicting types for '%s'" % canon)
                    ty = ty2
            if stype is None:
                if ty is None:
                    ty = self.opt_default
                if ty is None:
                    self.err("OPTION DEFAULT NONE: '%s' needs a type"
                             % canon)

            # MMBasic's DIM s$ LENGTH n, which caps a string to save
            # memory.
            #
            # On a SCALAR it is accepted and ignored: the string still
            # starts at its length byte with its characters after it, so
            # every address a program can compute is the same one, and a
            # generous cap only means a program that would hit "string
            # too long" on the firmware works here.
            #
            # On an ARRAY it is NOT free, because it sets the SPACING of
            # the elements: findvar returns val.s + nbr * (size + 1)
            # (MMBasic.c:4924).  PETSCII Robots holds its 128x64 world
            # map as DIM LV$(63) LENGTH 128 and reads tiles straight out
            # of it with PEEK(BYTE (y)*129 + x + lva) - the 129 IS the
            # LENGTH, and at MM_STRSZ spacing every tile read comes from
            # the wrong place.  So an array honours it, and the element
            # is the firmware's own: length byte plus LENGTH characters,
            # with no room for the trailing NUL the rest of the runtime
            # assumes.  That is the layout a STRING member of a TYPE
            # already has, so it takes the same care - mm_ssetm to
            # write, mm_scopy to read.
            slen = None
            if self.accept_kw('LENGTH'):
                if ty != TY_S:
                    self.err("LENGTH is only for strings, and '%s' is not "
                             "one" % canon)
                v = self.nxt()
                if v[0] != T_NUM or v[2] != 'I':
                    self.err("LENGTH takes a literal integer")
                if int(v[1]) < 1 or int(v[1]) > 255:
                    self.err("LENGTH must be 1..255")
                if dims and int(v[1]) < MM_STRLEN:
                    slen = int(v[1])

            if self.mode == 'decl':
                s = self.declare(canon, ty if stype is None else TY_I,
                                 scope, dims, static)
                if stype is not None:
                    s.stype = stype
                if slen is not None:
                    s.slen = slen
                if dyn or canon in self.redimmed:
                    dyn = True
                    if stype is not None:
                        self.err("an array of a TYPE needs constant "
                                 "bounds")
                    if len(dims) > 5:
                        self.err("an array has at most 5 dimensions")
                    s.dynamic = True
                    # A run-time array is a flat pointer plus a bounds
                    # table, and mm_arr_* walk it at MM_STRSZ: the
                    # spacing is the runtime's, not the declaration's.
                    s.slen = None
            else:
                s = self.lookup(canon)

            # A run-time bound is allocated where the DIM stands, not
            # hoisted: the expression may name variables that are only
            # set by the time the statement runs.
            if s is not None and s.dynamic and self.mode == 'emit':
                self.emit_dim_alloc(s, dims, False)

            if self.accept_op('='):
                if s is not None and s.stype is not None:
                    self.struct_initialiser(s)
                else:
                    if s is not None:
                        s.has_init = True
                    self.emit_initialiser(s, static)

            if not self.accept_op(','):
                break

    def dyn_decl(self, s, cn):
        """The C declaration of a run-time array's storage pointer."""
        if s.ty == TY_S:
            return 'char (*%s)[MM_STRSZ]' % cn
        return '%s *%s' % (CTYPE[s.ty], cn)

    def elsize(self, s):
        """The C size of one element of an array."""
        if s.ty == TY_S:
            return self.strsz(s)
        return 'sizeof(%s)' % CTYPE[s.ty]

    def strsz(self, s):
        """The declared size of one string element.

        MM_STRSZ unless the array was DIMmed with a LENGTH, in which
        case it is the firmware's own LENGTH + 1 - see do_declare.  A
        program cannot see the difference except through VARADDR, and
        one that looks is entitled to the firmware's answer.
        """
        if s.slen is None:
            return 'MM_STRSZ'
        return '%d' % (s.slen + 1)

    def sread(self, s, code):
        """Read a string ARRAY ELEMENT as an expression value.

        An element of a LENGTH array is the firmware's layout - no room
        for the trailing NUL when the string is full - so it is copied
        into a scratch buffer, which restores the invariant the rest of
        the runtime relies on (mm_cstr is s + 1).  This is exactly what
        a STRING member of a TYPE does, for exactly the same reason.
        """
        if s.ty != TY_S or s.slen is None:
            return code
        self.tmp_used = True
        return 'mm_scopy(%s)' % code

    def swrite(self, s, target, val):
        """Assign to a string variable or array element, bounded when
        the element is a LENGTH one (mm_ssetm writes the NUL only when
        the string leaves room for it)."""
        return self.swrite_cap(s.slen if s.ty == TY_S else None,
                               target, val)

    def swrite_cap(self, cap, target, val):
        """swrite where the caller carries the capacity rather than the
        symbol - what input_target hands back."""
        if cap is None:
            return 'mm_sset(%s, %s);' % (target, val)
        return 'mm_ssetm(%s, %d, %s);' % (target, cap, val)

    def emit_dim_alloc(self, s, dims, preserve):
        """DIM / REDIM of an array with run-time bounds.

        The new bounds go into a scratch table first and the runtime
        swaps them in, so a REDIM PRESERVE can compare the two before
        anything is allocated - and so a failed one leaves the array as
        it was rather than half changed.

        `dims` holds counts (the declaration adds the +1); the table
        holds MMBasic's UPPER BOUNDS, which is count - 1.
        """
        nb = self.newtmp('nb')
        old = self.newtmp('ao')
        self.tmp_used = True
        np = self.newtmp('an')
        self.emit('{ MMINTEGER %s[%d]; void *%s, *%s;'
                  % (nb, len(dims) + 1, old, np))
        self.emit('  %s[0] = %d;' % (nb, len(dims)))
        for k, d in enumerate(dims):
            self.emit('  %s[%d] = (%s) - 1;' % (nb, k + 1, d))
        # mm_heap and mm_lfree here rather than inside the runtime: under
        # bcrun only a call made BY the program reaches the VM's
        # allocator, so a block the native runtime malloc'd would be a
        # machine address in a cell the VM owns.
        self.uses_array = True
        self.emit('  %s = mm_heap((unsigned long)'
                  'mm_arr_bytes(%s, %s));' % (np, nb, self.elsize(s)))
        self.emit('  %s = mm_arr_swap(%s, %s, %s, %s, %s, %d);'
                  % (old, s.acc, s.bacc, nb, np, self.elsize(s),
                     1 if preserve else 0))
        # The PROGRAM stores the new pointer into its own variable: a
        # pointer-to-pointer would be written at the host's width into a
        # cell the VM sizes, which is a 32-bit slot under bcrun.
        self.emit('  %s = %s;' % (s.acc, np))
        self.emit('  if (%s) mm_lfree(%s); }' % (old, old))

    def do_redim(self):
        """REDIM [PRESERVE] a(n) [, b(n) ...]

        MMBasic's cmd_redim.  The array must already exist and must
        already be dynamic: an array declared with constant bounds is a
        C array, and there is nothing to re-dimension.
        """
        preserve = False
        if self.is_kw('PRESERVE'):
            self.i += 1
            preserve = True
        while True:
            t = self.nxt()
            if t[0] != T_ID:
                self.err("REDIM needs an array name")
            canon, sfx = split_suffix(t[1])
            s = self.lookup(canon)
            if s is None or not s.is_array:
                self.err("'%s' is not an array" % canon)
            if sfx is not None and sfx != s.ty:
                self.err("'%s' is %s but used as %s"
                         % (canon, TYNAME[s.ty], TYNAME[sfx]))
            if s.is_param:
                self.err("'%s' is a parameter, so its bounds belong to "
                         "the caller" % canon)
            if not s.dynamic:
                self.err("'%s' was DIMmed with constant bounds, so it has "
                         "no run-time size to change; give its DIM a "
                         "bound that is not a literal or a CONST" % canon)
            self.expect_op('(')
            dims = []
            while True:
                dims.append(self.count_of(self.as_int(self.expr())))
                if not self.accept_op(','):
                    break
            self.expect_op(')')
            if self.mode == 'emit':
                self.emit_dim_alloc(s, dims, preserve)
            if not self.accept_op(','):
                break

    def emit_initialiser(self, s, static):
        guard = None
        if static and self.mode == 'emit' and s.is_static:
            guard = '__once_' + s.name.replace('.', '__')
            self.emit('if (!%s) { %s = 1;' % (guard, guard))
            self.indent += 1
        if s.is_array:
            self.expect_op('(')
            k = 0
            while True:
                v = self.expr()
                if self.mode == 'emit':
                    sub = self.linear_index(s, k)
                    if s.ty == TY_S:
                        self.emit(self.swrite(s, sub, v[0]))
                    elif s.ty == TY_I:
                        self.emit('%s = %s;' % (sub, self.as_int(v)))
                    else:
                        self.emit('%s = %s;' % (sub, self.as_flt(v)))
                k += 1
                if not self.accept_op(','):
                    break
            self.expect_op(')')
        else:
            v = self.expr()
            if self.mode == 'emit':
                if s.ty == TY_S:
                    if v[1] != TY_S:
                        self.err("cannot assign a number to '%s'" % s.name)
                    self.emit('mm_sset(%s, %s);' % (s.acc, v[0]))
                elif s.ty == TY_I:
                    self.emit('%s = %s;' % (s.acc, self.as_int(v)))
                else:
                    self.emit('%s = %s;' % (s.acc, self.as_flt(v)))
        if guard is not None:
            self.indent -= 1
            self.emit('}')

    def linear_index(self, s, k):
        """Element k of an array in an initialiser list, in MMBasic's
        storage order.

        cmd_dim (Commands.c:8658) fills the values into linear array
        memory, and MMBasic arrays store the FIRST subscript varying
        fastest - so DIM a(3,1) = (p,q,...) sets a(0,0), a(1,0),
        a(2,0), a(3,0), a(0,1), ...  Our storage order is that one, so
        flat position k IS element k; what is left to do is turn k into
        the subscript LIST subscript() wants, the C array being
        declared with its dimensions reversed.  The divisions below are
        built from k (a literal) and the dimension sizes (constant
        expressions by the time an array is static), so cc1 folds every
        one of them to a plain index.

        What comes out is a C INDEX, not a BASIC subscript - this
        builds the accessor itself rather than going through
        subscript(), so nothing rebases it afterwards.  Element k of the
        list is C index k under either OPTION BASE now that BASE 1
        storage is dense, and adding the base here is what put
        DIM s(4) = (1,2,3,4) one place late and wrote off the end."""
        if len(s.dims) == 1:
            return '%s[%d]' % (s.acc, k)
        if s.dynamic:
            self.err("an initialiser list on a run-time DIM is only "
                     "supported for 1-D arrays")
        subs = []
        div = None
        for j, sz in enumerate(s.dims):
            u = self.usable(sz)
            if div is None:
                e = '%d' % k
            else:
                e = '(%d) / (%s)' % (k, div)
            if j < len(s.dims) - 1:
                e = '(%s) %% (%s)' % (e, u)
            subs.append('(%s)' % e)
            div = u if div is None else '(%s) * (%s)' % (div, u)
        return s.acc + ''.join('[' + p + ']' for p in reversed(subs))

    def do_const(self):
        while True:
            t = self.nxt()
            if t[0] != T_ID:
                self.err("CONST needs a name")
            canon, sfx = split_suffix(t[1])
            self.expect_op('=')
            v = self.expr()
            ty = v[1]
            if sfx is not None and sfx != ty:
                if sfx == TY_F and ty == TY_I:
                    v = (self.as_flt(v), TY_F)
                    ty = TY_F
                elif sfx == TY_I and ty == TY_F:
                    v = (self.as_int(v), TY_I)
                    ty = TY_I
                else:
                    self.err("CONST '%s' type conflict" % canon)
            if self.cur is not None:
                # CONST INSIDE A SUB OR FUNCTION IS LOCAL TO IT.
                #
                # MMBasic says so in one line - cmd_const does
                # `if (g_LocalIndex != 0) type |= V_LOCAL;`
                # (Commands.c:6478) - and this used to put every CONST
                # in the globals whatever scope it was written in.  Two
                # routines each declaring their own `Const f$` then
                # collided, and the second use of an unrelated `f%`
                # somewhere else in the program failed with "'f' is
                # STRING but used as INTEGER".  Six of the Game*Mite
                # programs do exactly that, because the library they
                # share declares CONSTs inside SELECT CASE arms.
                #
                # A local one is a LOCAL that is assigned where the
                # statement stands and refused as an assignment target
                # afterwards - not a #define.  That is what MMBasic
                # does: the expression is evaluated by DoExpression
                # when the statement runs, ONCE, and it may call a
                # function (3D-maze writes Const f$ =
                # gamemite.file$("menu.bas")).  Inlining it would call
                # that function again at every use.
                #
                # MMBasic also allows the same name twice in one
                # routine, because only one branch of an If runs;
                # declare() returns the existing symbol when the types
                # agree, so that keeps working.
                if self.mode == 'decl':
                    s = self.declare(canon, ty, 'local')
                    s.is_const = True
                else:
                    s = self.lookup(canon)
                if self.mode == 'emit' and s is not None:
                    if ty == TY_S:
                        self.emit('mm_sset(%s, %s);' % (s.acc, v[0]))
                    else:
                        self.emit('%s = %s;' % (s.acc, v[0]))
            elif self.mode == 'decl':
                s = Sym(canon, ty, '(' + v[0] + ')')
                s.is_const = True
                # An expression that is not compile-time constant must
                # be evaluated ONCE, where the statement stands, as
                # cmd_const's DoExpression does - never re-evaluated
                # from a #define at every use (see const_or_literal_expr)
                s.const_runtime = not const_or_literal_expr(v[0])
                s.where = self.lineno
                if canon in self.globals:
                    # A WARNING, and it has to be: MMBasic runs only one
                    # arm of an If, so the same global CONST declared in
                    # both arms is legal there and the arm that ran is
                    # the one that exists.  A compiler sees both and has
                    # to pick, and picking silently is how PicoMan came
                    # to draw its maze at (200,200) on a 320x240 screen:
                    # its Windows arm sets xMargin = 200 and its
                    # Game*Mite arm 10, and the first won.
                    #
                    # err() would be worse than useless here - the
                    # declaration pass swallows MMError, so it printed
                    # NOTHING and the wrong value shipped.
                    self.warn("'%s' is declared CONST more than once; "
                              "the first (line %d) is the one used"
                              % (canon, self.globals[canon].where))
                else:
                    self.globals[canon] = s
            elif self.mode == 'emit':
                s = self.globals.get(canon)
                if s is not None and s.is_const \
                        and getattr(s, 'const_runtime', False):
                    # evaluate once, in flow: the hidden global takes
                    # the value here and every use just reads it
                    if s.ty == TY_S:
                        self.emit('mm_sset(%s, %s);'
                                  % (cconst(canon), v[0]))
                    elif s.ty == TY_I:
                        self.emit('%s = %s;'
                                  % (cconst(canon), self.as_int(v)))
                    else:
                        self.emit('%s = %s;'
                                  % (cconst(canon), self.as_flt(v)))
            if not self.accept_op(','):
                break

    # ==================================================================
    #  passes 2 and 3 - walk every statement, scanning then emitting
    # ==================================================================

    def walk(self, mode):
        self.mode = mode
        self.in_type = False
        self.gosub_n = 0
        self.cur = None
        self.indent = 1
        self.err_window = 0
        self.err_window_pending = None
        self.blocks = []
        self.out = self.out_main
        self.opt_default = TY_F
        self.opt_explicit = False
        for idx in range(len(self.lines)):
            self.lineno = idx + 1
            try:
                self.toks = tokenize(self.lines[idx], self.lineno)
            except MMError as e:
                self.errors.append(str(e))
                continue
            if not self.toks:
                continue
            self.i = 0
            self.strip_line_number()
            while not self.at_end():
                if self.accept_op(':'):
                    continue
                try:
                    self.statement()
                except MMError as e:
                    if str(e) not in self.errors:
                        self.errors.append(str(e))
                    self.skip_statement()
        if self.blocks:
            self.errors.append("unterminated %s block (started line %d)"
                               % (self.blocks[-1][0], self.blocks[-1][-1]))

    # -- statement dispatch ---------------------------------------------
    def statement(self):
        """Wrapper that gives every statement a clean string scratch stack.

        String temporaries are only ever live inside one statement, so
        winding the scratch stack back to the enclosing function's mark
        before each statement keeps usage bounded no matter how long a
        loop runs."""
        where = len(self.out)
        out_at_entry = self.out
        ind = self.indent
        blocks_at_entry = list(self.blocks)
        tok_at_entry = self.i
        outer = self.tmp_used
        self.tmp_used = False
        failed = None
        try:
            self.statement_inner()
        except MMError as e:
            if not self.lenient:
                raise
            failed = str(e)
        finally:
            if self.mode == 'emit' and self.tmp_used \
                    and self.out is out_at_entry and failed is None:
                out_at_entry.insert(where,
                                    '    ' * ind + 'mm_release(__mark);')
            # Clear the poison and count the statement, exactly where the
            # interpreter does it: AFTER the statement (MMBasic.c:1867,
            # which is where SKIP n's "+1" goes).  It has to be after and
            # not before, or a statement that calls a SUB would have been
            # counted before the SUB's own statements ran, and the count
            # inside would be one short of the interpreter's.
            #
            # A statement that opened or closed a block is the exception:
            # by now it has emitted a brace, and the guard would land
            # inside it.  Those go in front instead - for an opener that
            # is the same thing (it runs once, either side), and for a
            # closer it lands at the end of the block, which is where the
            # closing keyword executes anyway.
            # A literal ON ERROR SKIP n arms its window here: the ON
            # ERROR statement itself is covered (n+1 with the runtime's
            # own +1), so ITS guard performs the decrement the
            # interpreter does at the end of the ON ERROR line, and the
            # counter reaches the next statement intact.
            if self.err_window_pending is not None and failed is None:
                self.err_window = self.err_window_pending + 1
            self.err_window_pending = None
            if self.mode == 'emit' and self.checks_on() \
                    and self.out is out_at_entry and failed is None:
                guard = ('    ' * ind
                         + 'if (__mm_e[1]) { mm_pr_commit(); __mm_e[0] = 0;'
                           ' if (__mm_e[1] > 0) __mm_e[1]--; }')
                if self.blocks == blocks_at_entry and len(self.out) > where:
                    out_at_entry.append(guard)
                else:
                    out_at_entry.insert(where, guard)
            if self.err_window > 0 and failed is None:
                self.err_window -= 1
            # The interrupt poll goes AFTER the error bookkeeping, which
            # is the interpreter's own order: statement, error
            # bookkeeping, then check_interrupt (MMBasic.c:1852-1879).
            # Same opener/closer placement rule as the guard above.
            if self.mode == 'emit' and self.uses_interrupts \
                    and self.out is out_at_entry and failed is None:
                poll = ('    ' * ind
                        + 'if (__mm_int_armed) mm_int_poll();')
                if self.blocks == blocks_at_entry and len(self.out) > where:
                    out_at_entry.append(poll)
                else:
                    out_at_entry.insert(where, poll)
            self.tmp_used = outer or self.tmp_used
        if failed is not None:
            self.skip_out(where, out_at_entry, ind, blocks_at_entry,
                          tok_at_entry, failed)

    def skip_out(self, where, out_at_entry, ind, blocks, tok_at_entry, why):
        """Undo whatever a failed statement emitted and leave a comment in
        its place, so one untranslatable line does not lose the rest of
        the program."""
        if self.out is out_at_entry:
            del out_at_entry[where:]
        self.indent = ind
        self.blocks = blocks
        self.skip_statement()
        text = self.source_text(tok_at_entry, self.i).strip()
        if not text:
            text = self.lines[self.lineno - 1].strip()
        reason = why
        if reason.startswith('line '):
            reason = reason.split(': ', 1)[-1]
        if self.mode == 'emit':
            self.skipped.append((self.lineno, text, reason))
            self.emit('/* MMBASIC line %d not translated: %s */'
                      % (self.lineno, cblock_safe(reason)))
            self.emit('/*     %s */' % cblock_safe(text))

    def routine_exit(self):
        """What has to run on every path OUT of a routine.

        Not the same thing as the release emitted after a statement or
        round a loop condition: those wind the scratch pools back to
        __mark and know nothing about the local block, which is not in
        them.  Only leaving the routine gives the block back, so every
        path out has to say so - and mm_error exits the process rather
        than unwinding, so those are all of them.
        """
        if self.cur is not None and self.cur.heap_locals:
            return 'mm_lfree(__L); mm_release(__mark);'
        return 'mm_release(__mark);'

    def loop_cond(self, c):
        """A loop test is re-evaluated every time round, so it needs its
        own release point."""
        return '(mm_release(__mark), (%s))' % c

    def cond_release(self):
        """Build a loop condition and say whether evaluating it consumes
        string temporaries.  Only then is the per-iteration release
        point needed: an unconditional one is a library call per trip
        that costs more than the body of a tight FOR loop."""
        outer = self.tmp_used
        self.tmp_used = False
        c = self.cond()
        used = self.tmp_used
        self.tmp_used = outer or self.tmp_used
        return c, used

    def statement_inner(self):
        t = self.peek()
        if t is None:
            return
        up = t[2] if t[0] == T_ID else t[1]

        if self.skip_type_block(up):
            return
        # A SUB the program defines WINS over a statement of the same
        # name - the rule the expression parser already applies to
        # functions, and for the same reason: a program written before
        # a command existed has to keep working.  tests/t2.bas has a
        # SUB Fill, which was a plain sub call until FILL became a
        # drawing command, and this is what keeps it one.
        #
        # Structural words are excluded: END, PRINT, FOR and the rest
        # are syntax, not commands, and a SUB called END could not be
        # called anyway.  Everything else is fair game.
        if t[0] == T_ID and up not in KEYWORDS \
                and split_suffix(t[1])[0] in self.routine_names:
            self.do_assign_or_call()
            return
        if up == 'STRUCT':
            self.i += 1
            self.do_struct()
            return
        if up == 'OPTION':
            self.i += 1
            self.do_option()
            self.skip_statement()
            return
        if up in ('DIM', 'LOCAL', 'STATIC', 'CONST'):
            self.i += 1
            self.do_declare(up)
            return
        if up == 'PRINT' or up == '?':
            self.i += 1
            self.do_print()
            return
        if up == 'LET':
            self.i += 1
            self.do_assign()
            return
        if up == 'IF':
            self.i += 1
            self.do_if()
            return
        if up == 'ELSEIF':
            self.i += 1
            self.do_elseif()
            return
        # MMBasic spells it two ways and AllCommands.h binds BOTH to
        # cmd_else: "Else If" is one command name there, not an ELSE
        # with an IF after it.  Taken as two words this opened a nested
        # block that wanted its own ENDIF, so a program written the
        # spelling the manual uses died with "unterminated if block".
        if up == 'ELSE' and self.is_kw('IF', 1):
            self.i += 2
            self.do_elseif()
            return
        if up == 'ELSE':
            self.i += 1
            self.do_else()
            return
        if up == 'ENDIF':
            self.i += 1
            self.close_block('if')
            return
        if up == 'FOR':
            self.i += 1
            self.do_for()
            return
        if up == 'NEXT':
            self.i += 1
            self.do_next()
            return
        if up == 'DO':
            self.i += 1
            self.do_do()
            return
        if up == 'LOOP':
            self.i += 1
            self.do_loop()
            return
        if up == 'WHILE':
            self.i += 1
            self.do_while()
            return
        if up == 'WEND':
            self.i += 1
            self.close_block('while')
            return
        if up == 'SELECT':
            self.i += 1
            self.do_select()
            return
        if up == 'CASE':
            self.i += 1
            self.do_case()
            return
        if up == 'EXIT':
            self.i += 1
            self.do_exit()
            return
        if up == 'GOTO':
            self.i += 1
            self.do_goto()
            return
        if up == 'SUB' or up == 'FUNCTION':
            self.i += 1
            self.open_routine(up == 'FUNCTION')
            return
        if up == 'END':
            self.i += 1
            self.do_end()
            return
        if up == 'CALL':
            self.i += 1
            self.do_callstmt()
            return
        if up == 'OPEN':
            self.i += 1
            self.do_open()
            return
        if up == 'CLOSE':
            self.i += 1
            self.do_close()
            return
        if up == 'FLUSH':
            # FLUSH #n - get what has been written onto the card.  One
            # channel, as MMBasic takes one; CLOSE above accepts a list
            # and this deliberately does not, because cmd_flush does
            # not.
            self.i += 1
            self.emit('mm_flush(%s);' % self.channel())
            return
        if up == 'INPUT':
            self.i += 1
            self.do_input()
            return
        if up == 'LINE' and self.is_kw('INPUT', 1):
            self.i += 2
            self.do_line_input()
            return
        if up == 'SEEK':
            self.i += 1
            fn = self.channel()
            self.expect_op(',')
            pos = self.expr()
            self.emit('mm_seek(%s, %s);' % (fn, self.as_int(pos)))
            return
        if up in ('KILL', 'MKDIR', 'RMDIR', 'CHDIR', 'FILES'):
            self.i += 1
            self.do_fileword(up)
            return
        if up == 'RENAME' or up == 'COPY':
            self.i += 1
            a = self.expr()
            if a[1] != TY_S:
                self.err("%s needs a file name string" % up)
            if up == 'RENAME':
                if not self.accept_kw('AS'):
                    self.err("RENAME old$ AS new$")
            else:
                if not self.accept_kw('TO'):
                    self.err("COPY from$ TO to$")
            b = self.expr()
            if b[1] != TY_S:
                self.err("%s needs a file name string" % up)
            self.emit('mm_%s(%s, %s);'
                      % ('rename' if up == 'RENAME' else 'copy', a[0], b[0]))
            return
        if up == 'DATA':
            self.i += 1
            self.skip_statement()          # gathered in the decl pass
            return
        if up == 'READ':
            self.i += 1
            self.do_read()
            return
        if up == 'RESTORE':
            self.i += 1
            self.do_restore()
            return
        if up == 'SORT':
            self.i += 1
            self.do_sort()
            return
        if up == 'CONTINUE':
            self.i += 1
            if self.accept_kw('FOR') or self.accept_kw('DO'):
                self.emit('continue;')
                return
            self.err("only CONTINUE FOR and CONTINUE DO can be translated")
        if up == 'INC':
            self.i += 1
            self.do_inc()
            return
        if up == 'CAT':
            self.i += 1
            self.do_cat()
            return
        if up == 'ERASE':
            self.i += 1
            self.do_erase()
            return
        if up == 'REDIM':
            self.i += 1
            self.do_redim()
            return
        if up == 'POKE':
            # POKE BYTE addr, value   and its wider relatives.
            #
            # The width is a bare keyword, exactly as PEEK's is, so it
            # is read here rather than as an argument.  MMBasic's other
            # POKE forms - VAR, DISPLAY, PROGMEM - address the
            # interpreter's own structures and have no equivalent.
            self.i += 1
            t = self.nxt()
            widths = {'BYTE': ('mmpk_poke_byte', False),
                      'SHORT': ('mmpk_poke_short', False),
                      'WORD': ('mmpk_poke_word', False),
                      'INTEGER': ('mmpk_poke_integer', False),
                      'FLOAT': ('mmpk_poke_float', True)}
            w = widths.get(t[2]) if t[0] == T_ID else None
            if w is None:
                self.err("POKE %s is not supported; translated are BYTE, "
                         "SHORT, WORD, INTEGER and FLOAT" % t[1])
            addr = self.as_int(self.expr())
            self.expect_op(',')
            v = self.expr()
            self.uses_peek = True
            self.emit('%s(%s, %s);'
                      % (w[0], addr,
                         self.as_flt(v) if w[1] else self.as_int(v)))
            return
        if up == 'CLEAR':
            self.i += 1
            self.warn("CLEAR zeroes every global; static storage cannot be "
                      "handed back the way the interpreter does")
            self.emit('__mmb_clear();')
            self.uses_clear = True
            return
        if up == 'CLS':
            # CLS [colour] - MMBasic floods the write buffer with it, so
            # this clears the off-screen framebuffer when one is
            # selected, not the screen.  No colour means the background
            # COLOUR set, which MM_CUR asks for.
            self.i += 1
            col = 'MM_CUR'
            if not self.stmt_end():
                col = self.as_int(self.expr())
            self.emit('mm_cls(%s);' % col)
            return
        if up == 'MODE':
            # MODE 1  640x480, one bit    MODE 2  320x240, 16 colours
            # The PicoMite VGA numbering, which is also the first two
            # HDMI modes; the runtime maps it onto the kernel's own.
            self.i += 1
            n = self.expr()
            self.emit('mm_mode(%s);' % self.as_int(n))
            return
        if up == 'FRAMEBUFFER':
            # FRAMEBUFFER CREATE | LAYER | CLOSE [F|L] | WRITE N|F|L |
            #             COPY s, d [, B] | MERGE [c] | WAIT
            #
            # MMBasic's Draw.c cmd_framebuffer.  Two off-screen buffers:
            # F, and the LAYER, which is another framebuffer in every
            # respect except that MERGE puts it OVER F on the way to the
            # screen, skipping a transparent colour.
            #
            # That is MMBasic's TFT model rather than its VGA/HDMI one,
            # where the layer is composited at scanout instead.  The
            # choice is argued in PC3-LAYER-MERGE.md and comes down to
            # SRAM: a scanout-time layer must live where core1 can DMA
            # it, which is 40K off every process forever.  A program
            # written for a PicoMite driving an ILI9341 runs unchanged.
            #
            # A mode change discards the buffers, both here and in the
            # kernel, so CREATE belongs after MODE - which is also
            # where MMBasic wants it, setmode() closing every buffer.
            self.i += 1
            if self.accept_kw('CREATE'):
                self.emit('mm_fb_create(1);')
                return
            if self.accept_kw('LAYER'):
                # FRAMEBUFFER LAYER [transparent] - the optional colour
                # (0-15, default 0) is the transparent index a MERGE
                # uses when it names none.  The firmware keeps it in
                # transparentlow/high (V5.08.00 Draw.c:7375-7381), so
                # it is run-time state and lives in an emitted global,
                # not in the translator.
                if not self.stmt_end():
                    self.uses_fbt = True
                    self.emit('__mm_fbt = (int)(%s);'
                              % self.as_int(self.expr()))
                self.emit('mm_fb_create(2);')
                return
            if self.accept_kw('CLOSE'):
                # CLOSE L closes the layer, CLOSE or CLOSE F the other
                which = 2 if self.accept_kw('L') else 1
                if which == 1:
                    self.accept_kw('F')
                self.emit('mm_fb_close(%d);' % which)
                return
            if self.accept_kw('MERGE'):
                # FRAMEBUFFER MERGE [colour] [, B] - the transparent
                # index, 0 to 15, defaulting to 0 as MMBasic's does.
                #
                # MMBasic's second argument asks for the merge to run on
                # the OTHER CORE so BASIC carries on (FrameBuffer.c:1071
                # pushes it down the multicore FIFO).  Accepted and not
                # acted on, and that is not a divergence: on a VGA
                # display the reference ignores it too - FrameBuffer.c
                # :1084 sets background = 0 for every DISPLAY_TYPE from
                # VGA222 up, which is this machine's class.  So the
                # merge happens in the syscall, exactly as it does
                # there.  R and A name modes this display has no
                # equivalent for and are refused rather than quietly
                # taken as B.
                c = '__mm_fbt' if self.uses_fbt else '0'
                if not self.stmt_end():
                    if not self.is_op(','):
                        c = self.as_int(self.expr())
                    if self.accept_op(','):
                        if not self.accept_kw('B'):
                            self.err('FRAMEBUFFER MERGE takes only B here')
                self.emit('mm_fb_merge(%s);' % c)
                return
            if self.accept_kw('WRITE'):
                self.emit('mm_fb_write(%s);' % self.fb_buf())
                return
            if self.accept_kw('COPY'):
                s = self.fb_buf()
                self.expect_op(',')
                d = self.fb_buf()
                b = 0
                if self.accept_op(','):
                    if not self.accept_kw('B'):
                        self.err("FRAMEBUFFER COPY takes only B here")
                    b = 1
                self.emit('mm_fb_copy(%s, %s, %d);' % (s, d, b))
                return
            if self.accept_kw('WAIT'):
                self.emit('mm_fb_wait();')
                return
            self.err("only FRAMEBUFFER CREATE, LAYER, CLOSE, WRITE, COPY, "
                     "MERGE and WAIT are translated")
        _loadprog = None
        if up == 'LOAD':
            for _kw, _p in (('IMAGE', 'loadimage'), ('BMP', 'loadimage'),
                            ('JPG', 'loadjpg'), ('PNG', 'loadpng')):
                if self.is_kw(_kw, 1):
                    _loadprog = _p
                    break
        if (up == 'SYSTEM' or _loadprog is not None
                or (up == 'SAVE' and self.is_kw('IMAGE', 1))):
            # SYSTEM prog$ [, arg ...]        run a program and wait
            # SAVE IMAGE f$ [, x, y, w, h]    both are programs
            # LOAD IMAGE f$ [, x, y]
            # LOAD BMP   f$ [, x, y]          the reference's own synonym
            # LOAD JPG   f$ [, x, y [, mode [, xi, yi [, scale]]]]
            # LOAD PNG   f$ [, x, y [, transparent [, cutoff]]]
            #
            # An argv, not a command line: nothing to quote and no
            # shell in the middle.  MMBasic has no SYSTEM - it is
            # firmware with nothing to run - so that spelling is ours,
            # but SAVE IMAGE and the LOAD family are the interpreter's
            # own and are simply handed to /usr/bin/saveimage,
            # /usr/bin/loadimage, /usr/bin/loadjpg and /usr/bin/loadpng.
            #
            # LOAD JPG's arguments are passed straight through in the
            # reference's order - x, y, dither mode, image offsets,
            # scale - so a program written for a PicoMite needs no
            # edit.  The mode is parsed and ignored there, as it is in
            # loadimage: see the note in loadjpg.c about dithering.
            if up == 'SYSTEM':
                self.i += 1
                prog = None
            else:
                self.i += 2
                prog = 'saveimage' if up == 'SAVE' else _loadprog
            self.emit('mm_run_begin();')
            if prog is not None:
                self.emit('mm_run_arg(%s);' % c_string_literal(prog))
            first = True
            while True:
                if not first and not self.accept_op(','):
                    break
                v = self.expr()
                if v[1] == TY_S:
                    self.emit('mm_run_arg(%s);' % v[0])
                elif v[1] == TY_I:
                    self.emit('mm_run_arg_i(%s);' % v[0])
                else:
                    self.emit('mm_run_arg_f(%s);' % v[0])
                first = False
            self.emit('mm_run_exec();')
            return
        if up == 'SPRITE':
            # The SPRITE family (graphics/Sprite.c), engine in
            # mmb_sprite.h on the BLIT row workhorses.  Deferred there
            # and refused here by name: SCROLL (Phase 4 of
            # PLAN-games.md - it wants the kernel's SCROLL2) and
            # LOADBMP (wants the BMP decoder).  LOADPNG translates.
            self.uses_sprite = True
            self.uses_blit = True
            if self.is_kw('MEMORY', 1) or self.is_kw('COMPRESSED', 1):
                # On an LCD PicoMite SPRITE and BLIT are one command
                # (V5.08.00's blitother serves both spellings), so the
                # memory forms are BLIT MEMORY under another name.
                self.do_blit_memform()
                return
            if self.is_kw('SHOW', 1):
                # SHOW [#]n,x,y,layer[,flags]
                # SHOW SAFE [#]n,x,y,layer[,flags[,ontop]] - flags may
                # be a bare comma in the SAFE form, as the reference
                # allows (argc >= 9 && *argv[8]).
                safe = '1LL' if self.is_kw('SAFE', 2) else '0LL'
                self.i += 3 if safe == '1LL' else 2
                self.accept_op('#')
                n = self.as_int(self.expr())
                self.expect_op(',')
                x = self.as_int(self.expr())
                self.expect_op(',')
                y = self.as_int(self.expr())
                self.expect_op(',')
                layer = self.as_int(self.expr())
                flags, ontop = '0LL', '0LL'
                if self.accept_op(','):
                    if not self.is_op(','):
                        flags = self.as_int(self.expr())
                    if safe == '1LL' and self.accept_op(','):
                        ontop = self.as_int(self.expr())
                self.emit('mms_show(%s, %s, %s, %s, %s, %s, %s);'
                          % (n, x, y, layer, flags, safe, ontop))
                return
            if self.is_kw('HIDE', 1):
                if self.is_kw('ALL', 2):
                    self.i += 3
                    self.emit('mms_hide_all();')
                    return
                safe = '1LL' if self.is_kw('SAFE', 2) else '0LL'
                self.i += 3 if safe == '1LL' else 2
                self.accept_op('#')
                n = self.as_int(self.expr())
                self.emit('mms_hide(%s, %s);' % (n, safe))
                return
            if self.is_kw('RESTORE', 1):
                self.i += 2
                self.emit('mms_restore();')
                return
            if self.is_kw('MOVE', 1):
                self.i += 2
                self.emit('mms_move();')
                return
            if self.is_kw('WRITE', 1):
                # WRITE [#]n,x,y[,flags] - the default is the
                # reference's rotation=4: transparency HONOURED, the
                # inverse of SHOW's default.
                self.i += 2
                self.accept_op('#')
                n = self.as_int(self.expr())
                self.expect_op(',')
                x = self.as_int(self.expr())
                self.expect_op(',')
                y = self.as_int(self.expr())
                flags = '4LL'
                if self.accept_op(','):
                    flags = self.as_int(self.expr())
                self.emit('mms_write(%s, %s, %s, %s);' % (n, x, y, flags))
                return
            if self.is_kw('READ', 1):
                self.i += 2
                self.accept_op('#')
                n = self.as_int(self.expr())
                self.expect_op(',')
                x = self.as_int(self.expr())
                self.expect_op(',')
                y = self.as_int(self.expr())
                self.expect_op(',')
                w = self.as_int(self.expr())
                self.expect_op(',')
                h = self.as_int(self.expr())
                self.emit('mms_read(%s, %s, %s, %s, %s);' % (n, x, y, w, h))
                return
            if self.is_kw('NEXT', 1):
                self.i += 2
                self.accept_op('#')
                n = self.as_int(self.expr())
                self.expect_op(',')
                x = self.as_int(self.expr())
                self.expect_op(',')
                y = self.as_int(self.expr())
                self.emit('mms_next(%s, %s, %s);' % (n, x, y))
                return
            if self.is_kw('COPY', 1):
                self.i += 2
                self.accept_op('#')
                n = self.as_int(self.expr())
                self.expect_op(',')
                self.accept_op('#')
                first = self.as_int(self.expr())
                self.expect_op(',')
                cnt = self.as_int(self.expr())
                self.emit('mms_copy(%s, %s, %s);' % (n, first, cnt))
                return
            if self.is_kw('SWAP', 1):
                self.i += 2
                self.accept_op('#')
                n = self.as_int(self.expr())
                self.expect_op(',')
                self.accept_op('#')
                rn = self.as_int(self.expr())
                flags = '0LL'
                if self.accept_op(','):
                    flags = self.as_int(self.expr())
                self.emit('mms_swap(%s, %s, %s);' % (n, rn, flags))
                return
            if self.is_kw('CLOSE', 1):
                if self.is_kw('ALL', 2):
                    self.i += 3
                    self.emit('mms_close_all();')
                    return
                self.i += 2
                self.accept_op('#')
                n = self.as_int(self.expr())
                self.emit('mms_close(%s);' % n)
                return
            if self.is_kw('LOADARRAY', 1):
                # LOADARRAY [#]n, w, h, array() - RGB888 colours,
                # reduced by RGB121 bit extraction as the reference
                # does.  An integer array; the reference also takes
                # float, which nothing has needed yet.
                self.i += 2
                self.accept_op('#')
                n = self.as_int(self.expr())
                self.expect_op(',')
                w = self.as_int(self.expr())
                self.expect_op(',')
                h = self.as_int(self.expr())
                self.expect_op(',')
                sym = self.arrayref()
                if sym.ty != TY_I:
                    self.err('SPRITE LOADARRAY wants an integer array')
                # ONE OR TWO dimensions.  MMBasic takes only one -
                # parsenumberarray with a dimension count of 1, and
                # "Argument 4 must be a 1D numerical array" otherwise -
                # and reads it as w*h pixels in sequence, which is what
                # the 1-D form does here too.
                #
                # The 2-D form is ours, and it costs no code: the first
                # BASIC subscript is the adjacent one, so DIM s(w-1,h-1)
                # walked flat IS the raster, row by row, with s(x, y) the
                # pixel a program would expect at x, y.
                if not (sym.is_param or sym.dynamic) \
                        and len(sym.dims) > 2:
                    self.err('SPRITE LOADARRAY wants a one- or '
                             'two-dimensional array')
                ptr, cnt = self.array_flat(sym)
                self.emit('mms_loadarray(%s, %s, %s, %s, %s);'
                          % (n, w, h, ptr, cnt))
                return
            if self.is_kw('LOAD', 1):
                # LOAD file$ [,startsprite [,mode]] - bare commas
                # allowed, as the reference's *argv[2] test allows.
                self.i += 2
                v = self.expr()
                if v[1] != TY_S:
                    self.err('SPRITE LOAD wants a file name')
                start, mode = '1LL', '0LL'
                if self.accept_op(','):
                    if not self.is_op(','):
                        start = self.as_int(self.expr())
                    if self.accept_op(','):
                        mode = self.as_int(self.expr())
                self.emit('mms_load(%s, %s, %s);' % (v[0], start, mode))
                return
            if self.is_kw('STATIC', 1):
                if self.is_kw('CLEAR', 2):
                    self.i += 3
                    self.emit('mms_static_clear();')
                    return
                self.i += 2
                self.accept_op('#')
                n = self.as_int(self.expr())
                self.expect_op(',')
                if self.is_kw('OFF'):
                    self.i += 1
                    self.emit('mms_static(%s, 0, 0, 0, 0, 1);' % n)
                    return
                x = self.as_int(self.expr())
                self.expect_op(',')
                y = self.as_int(self.expr())
                self.expect_op(',')
                w = self.as_int(self.expr())
                self.expect_op(',')
                h = self.as_int(self.expr())
                self.emit('mms_static(%s, %s, %s, %s, %s, 0);'
                          % (n, x, y, w, h))
                return
            if self.is_kw('SCROLL', 1):
                # SCROLL x, y [,colour] - the default is the
                # reference's -2: wrap the departing band round.
                self.i += 2
                x = self.as_int(self.expr())
                self.expect_op(',')
                y = self.as_int(self.expr())
                blank = '-2LL'
                if self.accept_op(','):
                    blank = self.as_int(self.expr())
                self.emit('mms_scroll(%s, %s, %s);' % (x, y, blank))
                return
            if self.is_kw('SET', 1) and self.is_kw('TRANSPARENT', 2):
                self.i += 3
                c = self.as_int(self.expr())
                self.emit('mms_set_transparent(%s);' % c)
                return
            if self.is_kw('INTERRUPT', 1):
                self.i += 2
                self.uses_interrupts = True
                self.emit('mmi_sprite_int(%s);' % self.int_handler())
                return
            if self.is_kw('NOINTERRUPT', 1):
                self.i += 2
                self.uses_interrupts = True
                self.emit('mmi_sprite_noint();')
                return
            if self.is_kw('STINTERRUPT', 1):
                self.i += 2
                self.uses_interrupts = True
                self.emit('mmi_st_int(%s);' % self.int_handler())
                return
            if self.is_kw('NOSTINTERRUPT', 1):
                self.i += 2
                self.uses_interrupts = True
                self.emit('mmi_st_noint();')
                return
            if self.is_kw('LOADPNG', 1):
                # SPRITE LOADPNG [#]n, f$ [, transparent [, cutoff]]
                #
                # The decoding is /usr/bin/loadpng's, in another
                # process, and the sprite comes back down a pipe - see
                # mms_loadpng.  transparent carries MMBasic's sign
                # trick (-n = substitute n for opaque black) and is
                # passed through untouched; cutoff defaults to 30 here,
                # not LOAD PNG's 20, as in the reference.
                self.i += 2
                self.accept_op('#')
                n = self.as_int(self.expr())
                self.expect_op(',')
                f = self.as_str(self.expr())
                t = '0LL'
                c = '30LL'
                if self.accept_op(','):
                    t = self.as_int(self.expr())
                    if self.accept_op(','):
                        c = self.as_int(self.expr())
                self.emit('mms_loadpng(%s, %s, %s, %s);'
                          % (n, f, t, c))
                return
            if self.is_kw('LOADBMP', 1):
                self.err('SPRITE LOADBMP is not translated')
            self.err('unknown SPRITE form')
        if up == 'BLIT':
            # BLIT READ [#]n, x, y, w, h        screen -> buffer 1-64
            # BLIT WRITE [#]n, x, y [, mode]    buffer -> screen, mode 0-7
            # BLIT CLOSE [#]n                   free the buffer
            # BLIT x1, y1, x2, y2, w, h         screen -> screen copy
            # BLIT COMPRESSED addr, x, y [, t]  RLE 4bpp image from memory
            # BLIT MEMORY addr, x, y [, t]      packed 4bpp, RLE if the
            #                                   size words carry the top bit
            #
            # cmd_blit (graphics/Blit.c), engine in mmb_blit.h.  WRITE's
            # mode argument is optional WITHOUT the bare-comma licence
            # the drawing commands have: the reference takes 5 or 7
            # arguments and nothing between (argc==6 is a syntax error).
            # The transparent colour is -1 (none) to 15, checked at run
            # time as the reference's getint does.
            #
            # BLIT FRAMEBUFFER s, d, x1, y1, x2, y2, w, h [, t]
            #                                   rectangle between N/F/L
            # BLIT FLASH n, d, x1, y1, x2, y2, w, h [, t]
            #                                   image out of a slot
            #
            # Not translated: LOAD (wants the BMP decoder), RESIZE, and
            # the LCD-only MERGE / RGB332-only MEMORY332, which do not
            # apply to these screen modes at all.
            self.uses_blit = True
            if self.is_kw('READ', 1):
                self.i += 2
                self.accept_op('#')
                n = self.as_int(self.expr())
                self.expect_op(',')
                x = self.as_int(self.expr())
                self.expect_op(',')
                y = self.as_int(self.expr())
                self.expect_op(',')
                w = self.as_int(self.expr())
                self.expect_op(',')
                h = self.as_int(self.expr())
                self.emit('mmb_blit_read(%s, %s, %s, %s, %s);'
                          % (n, x, y, w, h))
                return
            if self.is_kw('WRITE', 1):
                self.i += 2
                self.accept_op('#')
                n = self.as_int(self.expr())
                self.expect_op(',')
                x = self.as_int(self.expr())
                self.expect_op(',')
                y = self.as_int(self.expr())
                mode = '0LL'
                if self.accept_op(','):
                    mode = self.as_int(self.expr())
                self.emit('mmb_blit_write(%s, %s, %s, %s);'
                          % (n, x, y, mode))
                return
            if self.is_kw('CLOSE', 1):
                self.i += 2
                self.accept_op('#')
                n = self.as_int(self.expr())
                self.emit('mmb_blit_close(%s);' % n)
                return
            if self.is_kw('COMPRESSED', 1) or self.is_kw('MEMORY', 1):
                self.do_blit_memform()
                return
            if self.is_kw('FRAMEBUFFER', 1) or self.is_kw('FLASH', 1):
                is_flash = self.is_kw('FLASH', 1)
                self.i += 2
                if is_flash:
                    self.uses_flash = True
                    n = self.as_int(self.expr())
                    self.expect_op(',')
                    src = n
                else:
                    src = self.fb_buf()
                    self.expect_op(',')
                dst = self.fb_buf()
                args = []
                for _ in range(6):
                    self.expect_op(',')
                    args.append(self.as_int(self.expr()))
                blank = '-1LL'
                if self.accept_op(','):
                    blank = self.as_int(self.expr())
                self.emit('mmb_blit_%s(%s, %s, %s, %s, %s, %s, %s, %s, %s);'
                          % ('flash' if is_flash else 'fb', src, dst,
                             args[0], args[1], args[2], args[3], args[4],
                             args[5], blank))
                return
            for kw in ('LOAD', 'RESIZE', 'MERGE', 'MEMORY332'):
                if self.is_kw(kw, 1):
                    self.err('BLIT %s is not translated' % kw)
            self.i += 1
            x1 = self.as_int(self.expr())
            self.expect_op(',')
            y1 = self.as_int(self.expr())
            self.expect_op(',')
            x2 = self.as_int(self.expr())
            self.expect_op(',')
            y2 = self.as_int(self.expr())
            self.expect_op(',')
            w = self.as_int(self.expr())
            self.expect_op(',')
            h = self.as_int(self.expr())
            self.emit('mmb_blit_copy(%s, %s, %s, %s, %s, %s);'
                      % (x1, y1, x2, y2, w, h))
            return
        if up == 'FLASH':
            # FLASH DISK LOAD n, file$ [, O[VERWRITE]]
            # FLASH ERASE n
            #
            # The image-slot half of MMBasic's FLASH command
            # (FileIO.c:1232, :1039), against the pseudo slots of
            # mmb_flash.h.  The program-management half - SAVE, LOAD,
            # RUN, CHAIN, LIST - manages BASIC programs in flash, a
            # thing this machine does with a filesystem, so it is
            # refused by name rather than absorbed.
            self.uses_flash = True
            if self.is_kw('DISK', 1) and self.is_kw('LOAD', 2):
                self.i += 3
                n = self.as_int(self.expr())
                self.expect_op(',')
                v = self.expr()
                if v[1] != TY_S:
                    self.err('FLASH DISK LOAD wants a file name')
                ovr = '0LL'
                if self.accept_op(','):
                    if self.accept_kw('O') or self.accept_kw('OVERWRITE'):
                        ovr = '1LL'
                    else:
                        self.err('FLASH DISK LOAD takes only O here')
                self.emit('mmf_disk_load(%s, %s, %s);' % (v[0], n, ovr))
                return
            if self.is_kw('ERASE', 1):
                self.i += 2
                n = self.as_int(self.expr())
                self.emit('mmf_erase(%s);' % n)
                return
            self.err('only FLASH DISK LOAD and FLASH ERASE are translated')
        if up == 'PLAY':
            # PLAY MP3 f$          play a file, in the BACKGROUND
            # PLAY VOLUME n        0-100, remembered for later PLAYs
            # PLAY STOP            stop whatever is playing
            #
            # MMBasic's PLAY VOLUME takes a level per channel; this
            # takes one, because the volume reaches playmp3 as an
            # argument and playmp3 applies it to both.  Left and right
            # separately would mean a second argument that does nothing
            # yet, which is worse than not offering it.
            #
            # MP3 does NOT wait.  playmp3 is a separate process feeding
            # the kernel's ring, so the BASIC program carries on while
            # the music plays - the thing MMBasic needs checkWAVinput()
            # in its interpreter loop to manage, and which costs us
            # nothing.  That also means mm_run_exec cannot be used: it
            # waits.
            # STOP is first because MMBasic's cmd_play tests it first,
            # before it even checks that audio is configured: stopping
            # what is not playing is never an error.  It carries no
            # volume, so it does not set uses_play - a program whose
            # only PLAY is a STOP would then declare a variable it
            # never reads.
            if self.is_kw('STOP', 1):
                self.i += 2
                self.emit('mm_play_stop();')
                return
            self.uses_play = True
            if self.is_kw('VOLUME', 1):
                self.i += 2
                v = self.expr()
                if v[1] == TY_S:
                    self.err('PLAY VOLUME wants a number')
                if self.uses_playd:
                    # a running daemon hears the change at once
                    self.emit('mmp_volume(%s);' % self.as_int(v))
                else:
                    self.emit('mm_play_volume = (int)(%s);' % v[0])
                    self.emit('if (mm_play_volume < 0) mm_play_volume = 0;')
                    self.emit('if (mm_play_volume > 100) '
                              'mm_play_volume = 100;')
                return
            if self.is_kw('SOUND', 1):
                # PLAY SOUND voice, channel, type [, freq [, vol]]
                # channel: L R B M (M means both, as the reference
                # takes it); type: O S Q T W P N - U (a user table) is
                # not translated.  cmd_play at Audio.c:1946.
                self.uses_playd = True
                self.i += 2
                n = self.as_int(self.expr())
                self.expect_op(',')
                # Both of these are a bare letter, a quoted one or a
                # string the program works out - MMBasic's cmd_play
                # takes all three, and picofrog writes them quoted and
                # in lower case.
                sides = self.kw_or_str(
                    (('L', 1), ('R', 2), ('B', 3), ('M', 3)), 'mmp_side',
                    'PLAY SOUND wants a channel: L, R, B or M')
                self.expect_op(',')
                if self.is_kw('U'):
                    self.err('PLAY SOUND type U is not translated')
                ty = self.kw_or_str(
                    (('O', 0), ('S', 1), ('Q', 2), ('T', 3), ('W', 4),
                     ('P', 5), ('N', 6)), 'mmp_type',
                    'PLAY SOUND wants a type: O S Q T W P or N')
                freq, vol = '10.0', '25LL'
                if self.accept_op(','):
                    if not self.is_op(','):
                        freq = self.as_flt(self.expr())
                    if self.accept_op(','):
                        vol = self.as_int(self.expr())
                self.emit('mmp_sound(%s, %s, %s, %s, %s);'
                          % (n, sides, ty, freq, vol))
                return
            if self.is_kw('TONE', 1):
                # PLAY TONE left, right [, dur_ms [, interrupt]] - no
                # duration means until PLAY STOP; the completion
                # interrupt is a deadline here, not an IPC.
                self.uses_playd = True
                self.i += 2
                fl = self.as_flt(self.expr())
                self.expect_op(',')
                fr = self.as_flt(self.expr())
                dur, fn = '0.0', None
                if self.accept_op(','):
                    if not self.is_op(','):
                        dur = self.as_flt(self.expr())
                    if self.accept_op(','):
                        self.uses_interrupts = True
                        fn = self.int_handler()
                if fn is not None:
                    self.emit('mmi_tone_int(%s);' % fn)
                    self.emit('mmp_tone(%s, %s, %s, 1);' % (fl, fr, dur))
                else:
                    self.emit('mmp_tone(%s, %s, %s, 0);' % (fl, fr, dur))
                return
            if self.is_kw('MODFILE', 1):
                # PLAY MODFILE f$ [, interrupt] - with an interrupt
                # the song plays once and the player's exit fires it;
                # without one it loops until PLAY STOP.
                self.uses_playd = True
                self.i += 2
                v = self.expr()
                if v[1] != TY_S:
                    self.err('PLAY MODFILE wants a file name')
                fn = None
                if self.accept_op(','):
                    self.uses_interrupts = True
                    fn = self.int_handler()
                if fn is not None:
                    self.emit('mmi_mod_int(%s);' % fn)
                    self.emit('mmp_modfile(%s, 1);' % v[0])
                else:
                    self.emit('mmp_modfile(%s, 0);' % v[0])
                return
            if self.is_kw('MODSAMPLE', 1):
                # PLAY MODSAMPLE sample, channel [, volume] - a
                # request to the RUNNING player to mix a sample the
                # file already holds over the music.
                self.uses_playd = True
                self.i += 2
                sm = self.as_int(self.expr())
                self.expect_op(',')
                ch = self.as_int(self.expr())
                vol = '64LL'
                if self.accept_op(','):
                    vol = self.as_int(self.expr())
                self.emit('mmp_modsample(%s, %s, %s);' % (sm, ch, vol))
                return
            # MP3, WAV and FLAC are the same statement with a different
            # program behind it: each spawns a one-shot player that holds
            # the PCM stream for its own lifetime, so unlike SOUND and
            # MODFILE there is no daemon to command and NO KIND to
            # record - mmp_adopt says as much ("an MP3 player writes no
            # kind file"), and PLAY STOP reaches all three the same way,
            # by signalling whoever owns the stream.
            for kw, prog in (('MP3', 'playmp3'),
                             ('WAV', 'playwav'),
                             ('FLAC', 'playflac')):
                if self.is_kw(kw, 1):
                    self.i += 2
                    v = self.expr()
                    if v[1] != TY_S:
                        self.err('PLAY %s wants a file name' % kw)
                    # ... and each takes a completion interrupt, as
                    # MODFILE does.  It used to be DROPPED: the handler
                    # was left for the statement parser to trip over and
                    # the program played on with nothing armed.
                    fn = None
                    if self.accept_op(','):
                        self.uses_interrupts = True
                        fn = self.int_handler()
                    self.emit('mm_run_begin();')
                    self.emit('mm_run_arg(%s);' % c_string_literal(prog))
                    self.emit('mm_run_arg(%s);' % v[0])
                    self.emit('mm_run_arg_i(mm_play_volume);')
                    self.emit('mm_play_start();')
                    if fn is not None:
                        # after the spawn, so the watch starts with a
                        # player to wait for
                        self.emit('mmi_play_int(%s);' % fn)
                    return
            self.err('only PLAY MP3, WAV, FLAC, MODFILE, MODSAMPLE, SOUND, '
                     'TONE, VOLUME and STOP are translated')
        if up == 'CIRCLE':
            # CIRCLE x, y, r [, lw [, aspect [, colour [, fill]]]]
            # The geometry is mmb_gfx_circle.h's, not the runtime's.  MMBasic
            # treats an omitted argument as the default, so a bare
            # comma is legal in every position.
            self.i += 1
            x = self.as_int(self.expr())
            self.expect_op(',')
            y = self.as_int(self.expr())
            self.expect_op(',')
            r = self.as_int(self.expr())
            lw, asp, col, fill = '1LL', '1.0', 'MM_CUR', 'MM_CUR'
            if self.accept_op(','):
                if not self.is_op(','):
                    lw = self.as_int(self.expr())
                if self.accept_op(','):
                    if not self.is_op(','):
                        asp = self.as_flt(self.expr())
                    if self.accept_op(','):
                        if not self.is_op(','):
                            col = self.as_int(self.expr())
                        if self.accept_op(','):
                            fill = self.as_int(self.expr())
            self.uses_circle = True
            self.emit('mmg_circle(%s, %s, %s, %s, %s, %s, %s);'
                      % (x, y, r, lw, col, fill, asp))
            return
        if up in ('BOX', 'RBOX'):
            # BOX  x, y, w, h [, lw     [, colour [, fill]]]
            # RBOX x, y, w, h [, radius [, colour [, fill]]]
            #
            # cmd_box / cmd_rbox: width and height may be negative and
            # the box is drawn the other way; the line width (or the
            # corner radius) defaults to 1 (or 10); the colours default
            # to the current foreground and to no fill.  A bare comma
            # is legal in every optional position, as everywhere.
            is_rbox = (up == 'RBOX')
            self.i += 1
            x = self.as_int(self.expr())
            self.expect_op(',')
            y = self.as_int(self.expr())
            self.expect_op(',')
            w = self.as_int(self.expr())
            self.expect_op(',')
            h = self.as_int(self.expr())
            lw = '10LL' if is_rbox else '1LL'
            col, fill = 'MM_CUR', 'MM_CUR'
            if self.accept_op(','):
                if not self.is_op(','):
                    lw = self.as_int(self.expr())
                if self.accept_op(','):
                    if not self.is_op(','):
                        col = self.as_int(self.expr())
                    if self.accept_op(','):
                        fill = self.as_int(self.expr())
            if is_rbox:
                self.uses_rbox = True
                self.emit('mmg_rbox(%s, %s, %s, %s, %s, %s, %s);'
                          % (x, y, w, h, lw, col, fill))
            else:
                self.uses_box = True
                self.emit('mmg_box(%s, %s, %s, %s, %s, %s, %s);'
                          % (x, y, w, h, lw, col, fill))
            return
        if up == 'GUI' and self.is_kw('BITMAP', 1):
            # GUI BITMAP x, y, bits [,w] [,h] [,scale] [,c] [,bc]
            #
            # The one form of GUI that means anything here: the rest of
            # cmd_gui is touch-screen widgets, and this machine has no
            # touch hardware.  MMBasic's own defaults (Draw.c:449) are
            # 8x8 at scale 1 in the current colours - note the SCALE
            # default is 1 and not the FONT scale, whatever the manual
            # says; the code never reads the font.
            #
            # `bits` may be a string or an integer, and the two are
            # different byte sources rather than the same one converted
            # - see mmb_gui.h.
            self.i += 2
            self.uses_gui = True
            x = self.as_int(self.expr())
            self.expect_op(',')
            y = self.as_int(self.expr())
            self.expect_op(',')
            v = self.expr()
            w, h, scale = '8LL', '8LL', '1LL'
            fc, bc = 'mm_fg()', 'mm_bg()'
            for k in range(5):
                if not self.accept_op(','):
                    break
                if self.is_op(','):
                    continue            # a bare comma keeps the default
                e = self.as_int(self.expr())
                if k == 0:
                    w = e
                elif k == 1:
                    h = e
                elif k == 2:
                    scale = e
                elif k == 3:
                    fc = e
                else:
                    bc = e
            if v[1] == TY_S:
                self.emit('mmg_gui_bitmap(%s, %s, (const unsigned char *)'
                          'mm_cstr(%s), mm_slen(%s), %s, %s, %s, %s, %s);'
                          % (x, y, v[0], v[0], w, h, scale, fc, bc))
            elif v[1] == TY_I:
                self.emit('mmg_gui_bitmap_i(%s, %s, %s, %s, %s, %s, %s, %s);'
                          % (x, y, v[0], w, h, scale, fc, bc))
            else:
                self.err('GUI BITMAP wants a string or an integer')
            return
        if up == 'TRIANGLE':
            # TRIANGLE x1, y1, x2, y2, x3, y3 [, colour [, fill]]
            #
            # SAVE and RESTORE need the interpreter's blit buffers, so
            # only the drawing form is translated.  The colour may be a
            # bare comma, as everywhere.
            self.i += 1
            if self.accept_kw('SAVE') or self.accept_kw('RESTORE'):
                self.err("only the drawing form of TRIANGLE is "
                         "translated")
            x1 = self.as_int(self.expr())
            self.expect_op(',')
            y1 = self.as_int(self.expr())
            self.expect_op(',')
            x2 = self.as_int(self.expr())
            self.expect_op(',')
            y2 = self.as_int(self.expr())
            self.expect_op(',')
            x3 = self.as_int(self.expr())
            self.expect_op(',')
            y3 = self.as_int(self.expr())
            col, fill = 'MM_CUR', 'MM_CUR'
            if self.accept_op(','):
                if not self.is_op(','):
                    col = self.as_int(self.expr())
                if self.accept_op(','):
                    fill = self.as_int(self.expr())
            self.uses_triangle = True
            self.emit('mmg_triangle(%s, %s, %s, %s, %s, %s, %s, %s);'
                      % (x1, y1, x2, y2, x3, y3, col, fill))
            return
        if up == 'POLYGON':
            # POLYGON n, xarray(), yarray() [, bordercolour [, fillcolour]]
            #
            # Always closed - cmd_polygon passes close=1 to polygon();
            # the open form belongs to an internal GUI caller.  n == 0
            # means "as many as the array holds", which is MMBasic's
            # xcount == 0.
            #
            # The multi-polygon form, where the first argument is an
            # ARRAY of vertex counts and the coordinate arrays hold
            # several shapes end to end, is refused by name rather than
            # half-drawn.
            self.i += 1
            if self.is_array_arg():
                self.err("the multi-polygon form of POLYGON (a vertex "
                         "count array) is not translated; pass a count "
                         "and one polygon's points")
            nverts = self.as_int(self.expr())
            self.expect_op(',')
            xs = self.arrayref()
            xp, xn = self.array_flat(xs)
            self.expect_op(',')
            ys = self.arrayref()
            yp, yn = self.array_flat(ys)
            for s in (xs, ys):
                if s.ty == TY_S:
                    self.err("POLYGON needs numeric coordinate arrays, "
                             "and '%s' is a string array" % s.name)
            col, fill = 'MM_CUR', 'MM_CUR'
            if self.accept_op(','):
                if not self.is_op(','):
                    col = self.as_int(self.expr())
                if self.accept_op(','):
                    fill = self.as_int(self.expr())
            xf, xi = (xp, 'NULL') if xs.ty == TY_F else ('NULL', xp)
            yf, yi = (yp, 'NULL') if ys.ty == TY_F else ('NULL', yp)
            self.uses_polygon = True
            self.emit('mmg_polygon(%s, %s, %s, %s, %s, %s, %s, %s);'
                      % (xf, xi, yf, yi, nverts,
                         self.shortest([xn, yn]), col, fill))
            return
        if up == 'FILL':
            # FILL x, y, colour [, boundary]
            #
            # With a boundary the fill stops at that colour; without
            # one it replaces the colour at the starting point.  MM_CUR
            # carries "no boundary given" to the header, as everywhere.
            self.i += 1
            x = self.as_int(self.expr())
            self.expect_op(',')
            y = self.as_int(self.expr())
            self.expect_op(',')
            col = self.as_int(self.expr())
            bound = 'MM_CUR'
            if self.accept_op(','):
                bound = self.as_int(self.expr())
            self.uses_fill = True
            self.emit('mmg_fill(%s, %s, %s, %s);' % (x, y, col, bound))
            return
        if up == 'BEZIER':
            # BEZIER xarray(), yarray() [, n] [, colour]
            #
            # INTEGER arrays, which is MMBasic's own restriction -
            # cmd_bezier reads them with parseintegerarray.  A float
            # array is refused rather than converted, because it is a
            # program that would only work here.
            self.i += 1
            xs = self.arrayref()
            xp, xn = self.array_flat(xs)
            self.expect_op(',')
            ys = self.arrayref()
            yp, yn = self.array_flat(ys)
            for s in (xs, ys):
                if s.ty != TY_I:
                    self.err("BEZIER needs INTEGER control point arrays, "
                             "and '%s' is not one" % s.name)
            npts, col = '0LL', 'MM_CUR'
            if self.accept_op(','):
                if not self.is_op(','):
                    npts = self.as_int(self.expr())
                if self.accept_op(','):
                    col = self.as_int(self.expr())
            self.uses_bezier = True
            self.emit('mmg_bezier(%s, %s, %s, %s, %s);'
                      % (xp, yp, npts, self.shortest([xn, yn]), col))
            return
        if up == 'ARC':
            # ARC x, y, r1 [, r2], rad1, rad2 [, colour]
            #
            # An omitted r2 - a bare comma - is a one pixel wide arc at
            # r1, which cmd_arc expresses as r2 = r1, r1 - 1; MM_CUR
            # carries the omission to the header.  The angles are
            # MMBasic's compass degrees: 0 up, clockwise.
            self.i += 1
            x = self.as_int(self.expr())
            self.expect_op(',')
            y = self.as_int(self.expr())
            self.expect_op(',')
            r1 = self.as_int(self.expr())
            self.expect_op(',')
            r2 = 'MM_CUR'
            if not self.is_op(','):
                r2 = self.as_int(self.expr())
            self.expect_op(',')
            a1 = self.as_int(self.expr())
            self.expect_op(',')
            a2 = self.as_int(self.expr())
            col = 'MM_CUR'
            if self.accept_op(','):
                col = self.as_int(self.expr())
            self.uses_arc = True
            self.emit('mmg_arc(%s, %s, %s, %s, %s, %s, %s);'
                      % (x, y, r1, r2, a1, a2, col))
            return
        if up == 'TEXT':
            # TEXT x, y, string$ [, alignment$] [, font] [, scale]
            #                    [, colour] [, background]
            #
            # Every argument after the string is optional and a bare
            # comma is legal in any of them, as everywhere in MMBasic.
            # The two colours default to COLOUR's - resolved HERE, by
            # emitting mm_fg()/mm_bg(), because -1 is a colour TEXT
            # accepts (transparent paper) and so cannot double as the
            # "none given" sentinel the other statements use.
            self.i += 1
            x = self.as_int(self.expr())
            self.expect_op(',')
            y = self.as_int(self.expr())
            self.expect_op(',')
            s = self.as_str(self.expr())
            # 0 for an omitted font or scale, NOT 1: the default is the
            # CURRENT font and scale, which is what FONT set, and only
            # the runtime knows them.  Draw.c:2133 cmd_text takes both
            # from gui_font.  Emitting 1 here meant a program that said
            # FONT 10 and then drew with the plain four-argument TEXT -
            # which is what MMBasic programs do - got font 1 every time,
            # so an 8x8 panel came out in 8x12 and overlapped, and
            # DefineFont looked broken when it was not.
            just, font, scale = '0', '0LL', '0LL'
            fc, bc = 'mm_fg()', 'mm_bg()'
            if self.accept_op(','):
                if not self.is_op(','):
                    just = self.just_arg()
                if self.accept_op(','):
                    if not self.is_op(','):
                        font = self.as_int(self.expr())
                    if self.accept_op(','):
                        if not self.is_op(','):
                            scale = self.as_int(self.expr())
                        if self.accept_op(','):
                            if not self.is_op(','):
                                fc = self.as_int(self.expr())
                            if self.accept_op(','):
                                bc = self.as_int(self.expr())
            self.uses_text = True
            self.emit('mmg_text(%s, %s, %s, %s, %s, %s, %s, %s);'
                      % (x, y, s, just, font, scale, fc, bc))
            return
        if up == 'RTC' and (self.is_kw('GETREG', 1) or self.is_kw('SETREG', 1)):
            # RTC GETREG reg, var
            # RTC SETREG reg, value
            #
            # MMBasic's own pair (I2C.c cmd_rtc), and the way an alarm
            # is armed there - it has no alarm command.  Write the
            # match time into 0x07-0x0A, then INTCN|A1IE into 0x0E, and
            # the chip pulls GP32 low when the time comes.
            self.i += 1
            get = self.accept_kw('GETREG')
            if not get:
                self.accept_kw('SETREG')
            reg = self.as_int(self.expr())
            self.expect_op(',')
            if get:
                tgt = self.lvalue_from_here()
                self.emit('%s = mm_rtcreg(%s, 0, 0);' % (tgt, reg))
            else:
                self.emit('mm_rtcreg(%s, %s, 1);'
                          % (reg, self.as_int(self.expr())))
            return
        if up == 'I2C2':
            self.do_i2c2()
            return
        if up == 'I2C':
            self.do_i2c0()
            return
        if up == 'WEB':
            self.i += 1
            self.do_web()
            return
        if up == 'WATCHDOG':
            # Accepted and DELIBERATELY a no-op (PLAN-web.md 12.2):
            # the RP2350 watchdog belongs to the kernel on a
            # multi-process machine, and restart-on-death is an rc
            # loop here.  The warning is the honesty.
            self.i += 1
            self.warn('WATCHDOG is accepted but does nothing here: '
                      'the hardware watchdog belongs to the kernel; '
                      'use an rc restart loop for wedge recovery')
            if self.is_kw('OFF'):
                self.i += 1
            elif not self.stmt_end():
                self.as_int(self.expr())
            return
        if up == 'CPU':
            # CPU RESTART - the WebMite reboots; a process re-executes
            # itself (mm_restart execs argv[0]).  uses_cmdline so the
            # generated main binds argv and the name is known.
            self.i += 1
            if not self.is_kw('RESTART'):
                self.err('only CPU RESTART is supported')
            self.i += 1
            self.uses_cmdline = True
            self.emit('mm_restart();')
            return
        if up == 'ONEWIRE':
            self.i += 1
            self.do_onewire()
            return
        if up == 'TEMPR' and self.is_kw('START', 1):
            # TEMPR START pin [, precision [, timeout]] - begin a
            # conversion and come back for it later.  The reading form
            # is a function.
            self.i += 2
            pin = self.as_int(self.expr())
            prec = '1'
            tmo = '-1'
            if self.accept_op(','):
                if not (self.is_op(',') or self.stmt_end()):
                    prec = self.as_int(self.expr())
                if self.accept_op(','):
                    tmo = self.as_int(self.expr())
            self.uses_gpio = True
            self.uses_onewire = True
            self.emit('mmow_tempr_start(%s, %s, %s);' % (pin, prec, tmo))
            return
        if up == 'SPI' and not self.is_op('(', 1):
            # SPI( is the function - write a unit and read one back -
            # and it is handled in the expression parser.  A statement
            # starting with SPI is the command.
            self.do_spi()
            return
        if up == 'SETTICK':
            self.do_settick()
            return
        if up == 'PWM':
            # PWM slice, frequency, duty1 [, duty2]
            # PWM slice, OFF
            #
            # A SLICE, not a pin: one slice drives two pins, and SETPIN
            # pin, PWM is what attaches a pin to it.  MMBasic is the
            # same and for the same reason.
            self.i += 1
            self.uses_pwm = True
            slice_ = self.as_int(self.expr())
            self.expect_op(',')
            if self.accept_kw('OFF'):
                self.emit('mmp_pwm_off(%s);' % slice_)
                return
            freq = self.as_flt(self.expr())
            self.expect_op(',')
            d1 = self.as_flt(self.expr())
            # Channel B is optional and OMITTING IT LEAVES IT ALONE -
            # two outputs share a slice, so setting one must not stop
            # the other.  The flag says whether it was given; there is
            # no spare value to use as a sentinel, because a negative
            # duty already means inverted.
            if self.accept_op(','):
                d2, have2 = self.as_flt(self.expr()), '1'
            else:
                d2, have2 = '0.0', '0'
            self.emit('mmp_pwm2(%s, %s, %s, %s, %s);'
                      % (slice_, freq, d1, d2, have2))
            return
        if up == 'SERVO':
            # SERVO slice, position1 [, position2]
            # SERVO slice, OFF
            #
            # PWM at a 50Hz frame with the position as a pulse width;
            # MMBasic's mapping is duty = 5 + position * 0.05, so 0 is
            # 1ms, 50 is 1.5ms and 100 is 2ms.
            self.i += 1
            self.uses_pwm = True
            slice_ = self.as_int(self.expr())
            self.expect_op(',')
            if self.accept_kw('OFF'):
                self.emit('mmp_pwm_off(%s);' % slice_)
                return
            p1 = self.as_flt(self.expr())
            if self.accept_op(','):
                p2, have2 = self.as_flt(self.expr()), '1'
            else:
                p2, have2 = '0.0', '0'
            self.emit('mms_servo(%s, %s, %s, %s);'
                      % (slice_, p1, p2, have2))
            return
        if up == 'SETPIN':
            # SETPIN pin, DIN|DOUT
            #
            # The pin is the GPIO number, not MMBasic's connector-pin
            # numbering: the GPIO number is what the PC3 schematic, the
            # kernel and every other tool on this machine use, and a
            # second numbering for one statement would confuse more
            # than the incompatibility does.
            #
            # AIN and ARAW are the analogue modes and both need an ADC
            # pin: on the RP2350B channel n is GP40+n, and the header
            # brings out GP40-GP46.  ARAW reads the raw count, AIN the
            # voltage through MMBasic's sort-and-discard filter - the
            # difference is in mmg_pin_get, not here.
            self.i += 1
            pin = self.as_int(self.expr())
            self.expect_op(',')
            if self.accept_kw('DOUT'):
                mode = 'MMG_PIN_DOUT'
            elif self.accept_kw('DIN'):
                mode = 'MMG_PIN_DIN'
            elif self.accept_kw('AIN'):
                mode = 'MMG_PIN_AIN'
            elif self.accept_kw('ARAW'):
                mode = 'MMG_PIN_ARAW'
            elif self.accept_kw('OFF'):
                mode = 'MMG_PIN_OFF'
            elif self.accept_kw('INTH'):
                mode = 'MMG_PIN_INTH'
            elif self.accept_kw('INTL'):
                mode = 'MMG_PIN_INTL'
            elif self.accept_kw('INTB'):
                mode = 'MMG_PIN_INTB'
            elif self.accept_kw('PWM'):
                mode = 'MMG_PIN_PWM'
                self.uses_pwm = True
            elif self.accept_kw('FIN'):
                # SETPIN pin, FIN [, gate] - the counting inputs, and
                # CIN and PIN below.  GP4-GP7 only (MMBasic's INT1-INT4,
                # fixed on this machine - no OPTION COUNT), the one pin
                # family whose work lives in the kernel; mmb_gpio.h and
                # PLAN-count.md have the story.  The optional third
                # argument is validated at RUN time as MMBasic's getint
                # is - it can be an expression, like the pin.
                self.uses_gpio = True
                if self.accept_op(','):
                    arg = self.as_int(self.expr())
                else:
                    arg = '1000'
                self.emit('mmg_setpin_fin(%s, %s);' % (pin, arg))
                return
            elif self.accept_kw('CIN'):
                self.uses_gpio = True
                if self.accept_op(','):
                    arg = self.as_int(self.expr())
                else:
                    arg = '1'
                self.emit('mmg_setpin_cin(%s, %s);' % (pin, arg))
                return
            elif self.accept_kw('PIN'):
                # Period: the mode WORD is PIN, matched here before the
                # pin-pair fallthrough can read it as an expression -
                # MMBasic checkstrings it the same way (External.c:1604
                # area).
                self.uses_gpio = True
                if self.accept_op(','):
                    arg = self.as_int(self.expr())
                else:
                    arg = '1'
                self.emit('mmg_setpin_per(%s, %s);' % (pin, arg))
                return
            else:
                # SETPIN sda, scl, I2C2      - the pin-PAIR form
                # SETPIN p1, p2, p3, SPI     - the pin-TRIPLE form
                # Reached here because what followed the comma was not a
                # mode word but another pin.
                p2 = self.as_int(self.expr())
                self.expect_op(',')
                if self.accept_kw('I2C2'):
                    self.uses_i2c = True
                    # Remembered rather than acted on: MMBasic assigns
                    # the pins here and starts the controller at OPEN,
                    # and a program is entitled to do those in separate
                    # places.
                    self.emit('__mmi2c_sda = %s; __mmi2c_scl = %s;'
                              % (pin, p2))
                    return
                # a third pin, then SPI
                p3 = self.as_int(self.expr())
                self.expect_op(',')
                if not self.accept_kw('SPI'):
                    self.err("SETPIN takes DIN, DOUT, AIN, ARAW, "
                             "INTH, INTL, INTB, PWM, FIN, CIN, PIN "
                             "or OFF, or a pin pair followed by I2C2, "
                             "or a pin triple followed by SPI")
                self.uses_spi = True
                # Any order: which signal each pin carries is decided by
                # the pin number, not by its position here, exactly as
                # MMBasic works it out from PinDef[pin].mode rather than
                # from the order written.  mmb_spi.h sorts them.
                self.emit('__mmspi_a = %s; __mmspi_b = %s; __mmspi_c = %s;'
                          % (pin, p2, p3))
                return
            self.uses_gpio = True
            if mode.startswith('MMG_PIN_INT'):
                # SETPIN pin, INTH|INTL|INTB, handler [, PULLUP|PULLDOWN]
                self.expect_op(',')
                self.uses_interrupts = True
                fn = self.int_handler()
                self.emit('mmi_setpin_int(%s, %s, %s, %s);'
                          % (pin, mode, fn, self.setpin_pull()))
                return
            if mode == 'MMG_PIN_OFF' and self.uses_interrupts:
                # OFF has to disarm an interrupt as well as reset the
                # pin.  Only a program that arms one carries this.
                self.emit('mmi_setpin_off(%s);' % pin)
                return
            # SETPIN pin, DIN [, PULLUP|PULLDOWN].  MMBasic allows the
            # option on the input modes only; the others take no third
            # argument and one is refused rather than ignored.
            pull = self.setpin_pull() if mode == 'MMG_PIN_DIN' else '0'
            self.emit('mmg_setpin(%s, %s, %s);' % (pin, mode, pull))
            return
        if up == 'WS2812':
            # WS2812 type, pin, nbr, colours%()  - type is an unquoted
            # O|B|S|W, MMBasic's own spelling (External.c:4448); W is
            # the S timing with four colour bytes.  nbr = 1 may take a
            # scalar colour instead of an array (4482-4486), decided
            # here by the text as PIXEL's two forms are.  Emitted onto
            # the fixed PIO programs - PLAN-pioout.md, mmb_pioout.h.
            self.i += 1
            if self.accept_kw('O'):
                ty = 'MMG_WS_O'
            elif self.accept_kw('B'):
                ty = 'MMG_WS_B'
            elif self.accept_kw('S'):
                ty = 'MMG_WS_S'
            elif self.accept_kw('W'):
                ty = 'MMG_WS_W'
            else:
                self.err("WS2812 takes a type of O, B, S or W")
            self.expect_op(',')
            pin = self.as_int(self.expr())
            self.expect_op(',')
            nbr = self.as_int(self.expr())
            self.expect_op(',')
            self.uses_gpio = True
            self.uses_pioout = True
            if self.is_array_arg():
                s = self.arrayref()
                if s.ty != TY_I:
                    self.err("WS2812 wants an integer array")
                ptr, cnt = self.array_flat(s)
                self.emit('mmg_ws2812(%s, %s, %s, %s, %s);'
                          % (ty, pin, nbr, ptr, cnt))
            else:
                col = self.as_int(self.expr())
                self.emit('mmg_ws2812_one(%s, %s, %s, %s);'
                          % (ty, pin, nbr, col))
            return
        if up == 'BITSTREAM':
            # BITSTREAM pin, n, array() [, mode] - n timed transitions,
            # the array elements microseconds (float or integer arrays
            # both, as parsenumberarray takes); mode 1 is the
            # open-collector form.  Same engine as WS2812.
            self.i += 1
            pin = self.as_int(self.expr())
            self.expect_op(',')
            n = self.as_int(self.expr())
            self.expect_op(',')
            s = self.arrayref()
            if s.ty == TY_S:
                self.err("BITSTREAM wants a number array")
            ptr, cnt = self.array_flat(s)
            mode = '0'
            if self.accept_op(','):
                mode = self.as_int(self.expr())
            self.uses_gpio = True
            self.uses_pioout = True
            fn = ('mmg_bitstream_i' if s.ty == TY_I
                  else 'mmg_bitstream_f')
            self.emit('%s(%s, %s, %s, %s, %s);'
                      % (fn, pin, n, ptr, cnt, mode))
            return
        if up == 'PIN' and self.is_op('(', 1):
            # PIN(n) = value.  The reading form is a function, handled
            # in the expression parser; a statement starting with PIN
            # can only be the assignment.
            self.i += 1
            self.expect_op('(')
            pin = self.as_int(self.expr())
            self.expect_op(')')
            self.expect_op('=')
            val = self.as_int(self.expr())
            self.uses_gpio = True
            self.emit('mmg_pin_put(%s, %s);' % (pin, val))
            return
        if up in ('BIT', 'BYTE') and self.is_op('(', 1):
            # BIT(intvar, n) = 0|1        set or clear one bit
            # BYTE(strvar$, n) = 0..255   overwrite one character
            #
            # Both reach INTO a variable rather than replacing it, so
            # the target is an lvalue and not an expression - MMBasic
            # calls findvar and refuses a constant.  The TYPE check is
            # done here rather than at run time: the translator knows
            # what an lvalue is when it generates the call, so a BIT on
            # a string is a translation error naming the line, which is
            # better than MMBasic managing "Not an integer" at run time.
            self.i += 1
            self.expect_op('(')
            # lvalue_from_here, opened up: it returns the accessor and
            # drops the symbol, and the symbol is what carries the type.
            # Asking reference() a second time would register an implied
            # global twice.
            t2 = self.nxt()
            if t2[0] != T_ID:
                self.err("%s() assignment needs a variable" % up)
            s = self.reference(t2[1], self.is_op('('))
            tgt = self.index(s) if s.is_array else s.acc
            ty = s.ty
            if up == 'BIT' and ty != TY_I:
                self.err("BIT() assignment needs an integer variable")
            if up == 'BYTE' and ty != TY_S:
                self.err("BYTE() assignment needs a string variable")
            self.expect_op(',')
            n = self.as_int(self.expr())
            self.expect_op(')')
            self.expect_op('=')
            v = self.as_int(self.expr())
            self.uses_misc = True
            if up == 'BIT':
                self.emit('mm_bit_assign(&(%s), %s, %s);' % (tgt, n, v))
            else:
                self.emit('mm_byte_assign(%s, %s, %s);' % (tgt, n, v))
            return
        if up == 'FLAG' and self.is_op('(', 1):
            # FLAG(n) = 0|1 - one of the sixty-four scratch bits.  The
            # reading form is a function; a statement can only assign.
            self.i += 1
            self.expect_op('(')
            n = self.as_int(self.expr())
            self.expect_op(')')
            self.expect_op('=')
            v = self.as_int(self.expr())
            self.uses_misc = True
            self.emit('mm_flag_assign(%s, %s);' % (n, v))
            return
        if up == 'FLAGS' and self.is_op('=', 1):
            # FLAGS = value - all sixty-four at once.  Reading them is
            # MM.INFO(FLAGS), which is where MMBasic put it.
            self.i += 2
            self.uses_misc = True
            self.emit('mm_flags_set(%s);' % self.as_int(self.expr()))
            return
        if up == 'LMID' and self.is_op('(', 1):
            # LMID(a(), start [, num]) = s$
            #
            # A SPLICE, not an overwrite: num bytes come out and the
            # string goes in, so the long string changes length unless
            # the two match.  Leaving num out means "as many as the
            # replacement has" - see mm_ls_lmid.
            self.i += 1
            self.expect_op('(')
            ptr, cells = self.lsref()
            self.expect_op(',')
            start = self.as_int(self.expr())
            num = self.as_int(self.expr()) if self.accept_op(',') else '-1LL'
            self.expect_op(')')
            self.expect_op('=')
            v = self.expr()
            if v[1] != TY_S:
                self.err("LMID() assignment needs a string")
            self.uses_lstring = True
            self.emit('mm_ls_lmid(%s, %s, %s, %s, %s);'
                      % (ptr, cells, start, num, v[0]))
            return
        if up == 'PORT' and self.is_op('(', 1):
            # PORT(pin, nbits [, pin, nbits]...) = value
            #
            # Several output pins written as one number, in one masked
            # store per bank - see mmb_port.h for the bit order, which
            # is the part worth reading.  The pairs are written into
            # the runtime's table one call each, because FCC has no
            # compound literals and a pin number can be an expression,
            # so the static-table trick used for array bounds does not
            # apply either.
            self.i += 1
            self.expect_op('(')
            g = []
            while True:
                g.append(self.as_int(self.expr()))
                self.expect_op(',')
                g.append(self.as_int(self.expr()))
                if not self.accept_op(','):
                    break
            self.expect_op(')')
            self.expect_op('=')
            val = self.as_int(self.expr())
            if len(g) // 2 > 8:
                self.err('PORT takes at most 8 pin groups')
            self.uses_gpio = True
            self.uses_port = True
            for k in range(len(g) // 2):
                self.emit('mmg_port_group(%d, %s, %s);'
                          % (k, g[k * 2], g[k * 2 + 1]))
            self.emit('mmg_port_put(%d, %s);' % (len(g) // 2, val))
            return
        if up == 'MAP':
            # MAP(n) = colour     collect one entry
            # MAP SET             apply the collected palette
            # MAP RESET           back to the mode's own
            # MAP MAXIMITE        the Colour Maximite's sixteen
            # MAP GRAYSCALE       sixteen greys (GREYSCALE too)
            #
            # The function form MAP(n) is handled in the expression
            # parser; only the statement form can be followed by '='.
            self.i += 1
            if self.accept_kw('SET'):
                self.emit('mm_map_set();')
                return
            if self.accept_kw('RESET'):
                self.emit('mm_map_reset();')
                return
            if self.accept_kw('MAXIMITE'):
                self.uses_mappal = True
                self.emit('mmg_map_maximite();')
                return
            if self.accept_kw('GRAYSCALE') or self.accept_kw('GREYSCALE'):
                self.uses_mappal = True
                self.emit('mmg_map_greyscale();')
                return
            self.expect_op('(')
            n = self.as_int(self.expr())
            self.expect_op(')')
            self.expect_op('=')
            c = self.as_int(self.expr())
            self.emit('mm_map(%s, %s);' % (n, c))
            return
        if up == 'FONT':
            # FONT [#]n [, scale] - the font PRINT draws in.  MMBasic
            # allows the # and ignores it, as it does on file numbers.
            self.i += 1
            self.accept_op('#')
            n = self.as_int(self.expr())
            scale = '1LL'
            if self.accept_op(','):
                scale = self.as_int(self.expr())
            self.emit('mm_font(%s, %s);' % (n, scale))
            return
        if up in ('COLOUR', 'COLOR'):
            self.i += 1
            if self.is_kw('MAP'):
                # COLOUR MAP in%(), out%() [, map%()] - a whole array of
                # colour codes 0-15 turned into RGB888.  The array form
                # of MAP(), and it shares mm_map_get with it, so the
                # default palette can only be described in one place.
                #
                # Integer arrays only.  MMBasic's parsenumberarray takes
                # float ones too, but every spelling in its manual is %
                # and a float palette would double the runtime for no
                # program that exists.
                self.i += 1
                src = self.arrayref()
                self.expect_op(',')
                dst = self.arrayref()
                cmap, cmapn = 'NULL', '0'
                if self.accept_op(','):
                    m = self.arrayref()
                    if m.ty != TY_I:
                        self.err("COLOUR MAP's palette must be an integer "
                                 "array")
                    cmap, cmapn = self.array_line(m)
                if src.ty != TY_I or dst.ty != TY_I:
                    self.err("COLOUR MAP works on integer arrays")
                sp, sc = self.array_flat(src)
                dp, dc = self.array_flat(dst)
                self.uses_misc = True
                self.emit('mm_colour_map(%s, %s, %s, %s, %s, %s);'
                          % (sp, sc, dp, dc, cmap, cmapn))
                return
            # COLOUR fg [, bg].  Everything that draws without being
            # given a colour uses fg.  bg is remembered but nothing
            # reads it yet - TEXT and the filled shapes will.
            fg = self.expr()
            if self.accept_op(','):
                bg = self.as_int(self.expr())
            else:
                bg = 'MM_CUR'
            self.emit('mm_colour(%s, %s);' % (self.as_int(fg), bg))
            return
        if up == 'PIXEL':
            # PIXEL x, y        - in the current foreground colour
            # PIXEL x, y, c     - c is RGB888, as everywhere in MMBasic;
            #                     the kernel primitive converts it to
            #                     whatever the current mode uses.
            # PIXEL xa(), ya() [, c | ca()]   - a whole run of points
            #
            # The function form PIXEL(x,y) is handled in the expression
            # parser; a statement never starts with the open bracket.
            self.i += 1
            if self.is_array_arg():
                self.do_pixels()
                return
            x = self.expr()
            self.expect_op(',')
            y = self.expr()
            if self.is_op(','):
                self.i += 1
                c = self.expr()
                col = self.as_int(c)
            else:
                col = 'MM_CUR'
            self.emit('mm_pixel(%s, %s, %s);'
                      % (self.as_int(x), self.as_int(y), col))
            return
        if up == 'LINE':
            # LINE x1, y1, x2, y2 [, width [, colour]]
            #
            # The geometry is in the runtime, not the kernel: an
            # axis-aligned line becomes one span and the rest is
            # Bresenham into a batch, so the whole line crosses into the
            # kernel once.  Measured 433us point-by-point against 71us
            # batched for a 312 point diagonal.
            self.i += 1
            x1 = self.expr()
            self.expect_op(',')
            y1 = self.expr()
            self.expect_op(',')
            x2 = self.expr()
            self.expect_op(',')
            y2 = self.expr()
            col = 'MM_CUR'
            wid = None
            if self.accept_op(','):
                # x1..y2 are required; the optional ones after them may
                # each be left blank, so LINE x1,y1,x2,y2,,c is how a
                # colour is given without a width.
                if not self.is_op(','):
                    w = self.expr()
                    if w[0].strip() not in ('1LL', '1'):
                        wid = self.as_int(w)
                if self.accept_op(','):
                    col = self.as_int(self.expr())
            if wid is None:
                self.emit('mm_line(%s, %s, %s, %s, %s);'
                          % (self.as_int(x1), self.as_int(y1),
                             self.as_int(x2), self.as_int(y2), col))
            else:
                # A width is four different algorithms in the firmware,
                # picked by shape - see mmb_gfx_line.h.  Only a program
                # that asks for one carries them.
                self.uses_linew = True
                self.emit('mmg_linew(%s, %s, %s, %s, %s, %s);'
                          % (self.as_int(x1), self.as_int(y1),
                             self.as_int(x2), self.as_int(y2), wid, col))
            return
        if up == 'PAUSE':
            # mm_wait, not mm_pause, for a program with an interrupt or
            # a PULSE to service: it is the same wait cut into slices
            # with the poll between them, which is what makes a SETTICK
            # handler fire during a PAUSE the way MMBasic's does.  A
            # program with nothing armed emits the plain one and pays
            # nothing.  Decided in the scan pass, so a PAUSE textually
            # ahead of the SETTICK still gets the serviced form.
            self.i += 1
            v = self.expr()
            if self.uses_interrupts or self.uses_pulse:
                self.uses_wait = True
                self.emit('mm_wait(%s);' % self.as_flt(v))
            else:
                self.emit('mm_pause(%s);' % self.as_flt(v))
            return
        if up == 'PULSE':
            # PULSE pin, width_ms - invert the pin for that long.  Under
            # 3 ms it blocks and is exact; longer and it returns at once
            # and the pin flips back later.  See mmb_pulse.h.
            self.i += 1
            pin = self.as_int(self.expr())
            self.expect_op(',')
            width = self.as_flt(self.expr())
            self.uses_gpio = True
            self.uses_pulse = True
            self.emit('mmg_pulse(%s, %s);' % (pin, width))
            return
        if up == 'ERROR':
            self.i += 1
            if self.stmt_end():
                self.emit('mm_error("Program halted by ERROR");')
            else:
                v = self.expr()
                if v[1] != TY_S:
                    self.err("ERROR needs a message string")
                self.emit('mm_error_s(%s);' % v[0])
            return
        if up == 'ON' and self.is_kw('ERROR', 1):
            self.i += 2
            self.do_on_error()
            return
        if up == 'ON' and self.is_kw('KEY', 1):
            self.i += 2
            self.do_on_key()
            return
        if up == 'ON' and self.peek(1) is not None \
                and not self.is_kw('ERROR', 1) and not self.is_kw('KEY', 1) \
                and not self.is_kw('PS2', 1):
            self.i += 1
            self.do_on_goto()
            return
        if up == 'ARRAY':
            self.i += 1
            self.do_array_cmd(False)
            return
        if up == 'MATH' and not self.is_op('(', 1):
            self.i += 1
            self.do_array_cmd(True)
            return
        if up in ('TIMER', 'DATE$', 'TIME$') and self.is_op('=', 1):
            self.i += 2
            v = self.expr()
            if up == 'TIMER':
                self.emit('mm_timer_set(%s);' % self.as_flt(v))
            elif v[1] != TY_S:
                self.err("%s = needs a string" % up)
            else:
                self.emit('mm_set_%s(%s);'
                          % ('date' if up == 'DATE$' else 'time', v[0]))
            return
        if up == 'LONGSTRING':
            self.i += 1
            self.do_longstring()
            return
        if up == 'GOSUB':
            self.i += 1
            self.do_gosub()
            return
        if up == 'RETURN':
            self.i += 1
            self.do_return()
            return
        if up == 'RANDOMIZE':
            self.i += 1
            v = self.expr()
            self.emit('mm_randomize(%s);' % self.as_int(v))
            return
        if t[0] == T_ID:
            self.do_assign_or_call()
            return
        self.err("cannot parse statement starting with '%s'" % t[1])

    # -- PRINT -----------------------------------------------------------
    def do_print(self):
        chan = None
        if self.is_op('#'):
            chan = self.channel()
            self.accept_op(',')          # the comma after #n is not a tab
        suppress_nl = False
        last = None                  # where the last item was emitted
        while not self.stmt_end():
            if self.accept_op(';'):
                suppress_nl = True
                continue
            if self.accept_op(','):
                self.emit(self.prcall(chan, 'tab', None))
                last = self.last_line()
                suppress_nl = True
                continue
            if self.is_op('@'):
                # PRINT @(x, y [, mode]) - MMBasic's fun_at.  It is a
                # FUNCTION returning "", not a statement, so it sits in
                # the item list like anything else and needs no line of
                # its own; MMBasic parses it the same way, which is why
                # nothing separates it from the text that follows.
                self.i += 1
                self.expect_op('(')
                x = self.as_int(self.expr())
                self.expect_op(',')
                y = self.as_int(self.expr())
                mode = '0'
                if self.accept_op(','):
                    mode = self.as_int(self.expr())
                self.expect_op(')')
                if chan is not None:
                    self.err("PRINT @ positions text on the screen, so it "
                             "cannot be used with a file channel")
                # mm_at returns a string temporary, so the statement has
                # to release the previous one.  Without this a PRINT @
                # inside a loop runs out of temporaries after MM_TMPN
                # turns and dies with "String expression too complex" -
                # which is exactly the shape a counter redrawn every
                # frame has.
                self.tmp_used = True
                self.emit(self.prcall(chan, 's',
                                      'mm_at(%s, %s, %s)' % (x, y, mode)))
                last = self.last_line()
                suppress_nl = False
                continue
            v = self.expr()
            suppress_nl = False
            if v[1] == TY_S:
                self.emit(self.prcall(chan, 's', v[0]))
            elif v[1] == TY_I:
                self.emit(self.prcall(chan, 'i', v[0]))
            else:
                self.emit(self.prcall(chan, 'f', v[0]))
            last = self.last_line()
        if not suppress_nl:
            self.emit(self.prcall(chan, 'nl', None))
        elif chan is None and last is not None:
            # PRINT "x"; - no newline, but the text still belongs on
            # screen now.  stdio is line buffered on a terminal, so
            # without a flush it waits for the NEXT newline: a program
            # that prints "Calculating... " and then works for half a
            # minute shows nothing until it has finished.  A file
            # channel needs no such thing and would only be slowed.
            #
            # The flush rides on the LAST item's call - mm_pr_s becomes
            # mm_pr_se - rather than being a statement after it.  One
            # extra statement in main cost the KnivD benchmark 32,400
            # grains against 12,150, because on the board's compiler it
            # tips the function out of native code; the host build does
            # not do it, so no gate would have caught it.
            self.out[last] = self.out[last].replace('(', 'e(', 1)

    def prcall(self, chan, what, arg):
        if chan is None:
            return 'mm_pr_%s(%s);' % (what, arg if arg is not None else '')
        if arg is None:
            return 'mm_fpr_%s(%s);' % (what, chan)
        return 'mm_fpr_%s(%s, %s);' % (what, chan, arg)

    # -- LONGSTRING ---------------------------------------------------------
    def do_longstring(self):
        t = self.nxt()
        if t[0] != T_ID:
            self.err("LONGSTRING needs a sub-command")
        op = t[2]
        self.uses_lstring = True

        if op in ('CLEAR', 'UCASE', 'LCASE'):
            ptr, cells = self.lsref()
            if op == 'CLEAR':
                self.emit('mm_ls_clear(%s, %s);' % (ptr, cells))
            else:
                self.emit('mm_ls_%s(%s);' % (op.lower(), ptr))
            return

        if op in ('APPEND', 'REPLACE'):
            ptr, cells = self.lsref()
            self.expect_op(',')
            v = self.expr()
            if v[1] != TY_S:
                self.err("LONGSTRING %s needs a normal string" % op)
            if op == 'APPEND':
                self.emit('mm_ls_append(%s, %s, %s);' % (ptr, cells, v[0]))
                return
            self.expect_op(',')
            st = self.expr()
            self.emit('mm_ls_replace(%s, %s, %s, %s);'
                      % (ptr, cells, v[0], self.as_int(st)))
            return

        if op == 'LOAD':
            ptr, cells = self.lsref()
            self.expect_op(',')
            n = self.expr()
            self.expect_op(',')
            v = self.expr()
            if v[1] != TY_S:
                self.err("LONGSTRING LOAD needs a normal string")
            self.emit('mm_ls_load(%s, %s, %s, %s);'
                      % (ptr, cells, self.as_int(n), v[0]))
            return

        if op in ('COPY', 'CONCAT'):
            dptr, dcells = self.lsref()
            self.expect_op(',')
            sptr, scells = self.lsref()
            self.emit('mm_ls_%s(%s, %s, %s);'
                      % (op.lower(), dptr, dcells, sptr))
            return

        if op in ('LEFT', 'RIGHT', 'MID'):
            dptr, dcells = self.lsref()
            self.expect_op(',')
            sptr, scells = self.lsref()
            self.expect_op(',')
            a = self.expr()
            if op == 'MID':
                b = '-1LL'
                if self.accept_op(','):
                    b = self.as_int(self.expr())
                self.emit('mm_ls_mid(%s, %s, %s, %s, %s);'
                          % (dptr, dcells, sptr, self.as_int(a), b))
                return
            self.emit('mm_ls_%s(%s, %s, %s, %s);'
                      % (op.lower(), dptr, dcells, sptr, self.as_int(a)))
            return

        if op in ('RESIZE', 'TRIM'):
            ptr, cells = self.lsref()
            self.expect_op(',')
            n = self.expr()
            self.emit('mm_ls_%s(%s, %s, %s);'
                      % (op.lower(), ptr, cells, self.as_int(n)))
            return

        if op == 'SETBYTE':
            ptr, cells = self.lsref()
            self.expect_op(',')
            n = self.expr()
            self.expect_op(',')
            v = self.expr()
            self.emit('mm_ls_setbyte(%s, %s, (%s) - %d, %s);'
                      % (ptr, cells, self.as_int(n), self.opt_base,
                         self.as_int(v)))
            return

        if op == 'PRINT':
            chan = '0'
            if self.is_op('#'):
                chan = self.channel()
                self.accept_op(',')
            ptr, cells = self.lsref()
            nl = '1'
            if self.accept_op(';') or self.accept_op(','):
                nl = '0'
            self.emit('mm_ls_print(%s, %s, %s);' % (chan, ptr, nl))
            return

        self.err("LONGSTRING %s is not supported" % t[1])

    # -- GOSUB / RETURN ------------------------------------------------------
    def gosub_key(self):
        return self.cur.name if self.cur else ''

    def do_gosub(self):
        t = self.nxt()
        if t[0] == T_NUM and t[2] == 'I':
            canon = t[1]
        elif t[0] == T_ID:
            canon = split_suffix(t[1])[0]
        else:
            self.err("GOSUB needs a label or line number")
        self.emit_gosub(canon, t[1])

    def emit_gosub(self, canon, disp):
        if canon not in self.labels:
            self.err("unknown label '%s'" % disp)
        here = self.gosub_key()
        there = self.label_routine.get(canon, '')
        if here != there:
            self.err("GOSUB '%s' crosses a SUB/FUNCTION boundary; C cannot "
                     "jump between functions, so move the target or use a "
                     "SUB" % disp)
        self.gosub_n += 1
        site = self.gosub_n
        self.labels_used[canon] = 1
        self.note_goto(canon)
        if self.mode == 'scan':
            self.gosub_sites.setdefault(here, []).append(site)
        self.uses_misc = True
        self.emit('mm_gosub_push(%d); goto %s;' % (site, clabel(canon)))
        self.raw('__GR%d: ;' % site)

    def do_return(self):
        sites = self.gosub_sites.get(self.gosub_key(), [])
        if not sites:
            self.err("RETURN without any GOSUB in this part of the program")
        self.uses_misc = True
        self.emit('switch (mm_gosub_pop()) {')
        for site in sites:
            self.emit('    case %d: goto __GR%d;' % (site, site))
        self.emit('    default: mm_error("RETURN without GOSUB");')
        self.emit('}')

    # -- DATA / READ / RESTORE ---------------------------------------------
    def do_read(self):
        self.uses_data = True
        if self.accept_kw('SAVE'):
            self.emit('mm_read_save();')
            return
        if self.accept_kw('RESTORE'):
            self.emit('mm_read_unsave();')
            return
        while not self.stmt_end():
            t = self.peek()
            if t is not None and t[0] == T_ID and self.is_op('(', 1) \
                    and self.is_op(')', 2):
                sym = self.arrayref()
                ptr, cnt = self.array_flat(sym)
                k = self.newtmp('k')
                self.emit('{ int %s; for (%s = 0; %s < %s; %s++)'
                          % (k, k, k, cnt, k))
                if sym.ty == TY_S:
                    self.reads_string = True
                    self.emit('    mm_sset((%s)[%s], mm_read_s()); }'
                              % (ptr, k))
                else:
                    self.emit('    (%s)[%s] = mm_read_%s(); }'
                              % (ptr, k, 'i' if sym.ty == TY_I else 'f'))
                self.tmp_used = True
            else:
                tgt, ty, cap = self.input_target()
                if ty == TY_S:
                    self.reads_string = True
                    self.emit(self.swrite_cap(cap, tgt, 'mm_read_s()'))
                    self.tmp_used = True
                else:
                    self.emit('%s = mm_read_%s();'
                              % (tgt, 'i' if ty == TY_I else 'f'))
            if not self.accept_op(','):
                break

    def do_restore(self):
        self.uses_data = True
        if self.stmt_end():
            self.emit('mm_restore(0);')
            return
        t = self.nxt()
        if t[0] == T_NUM and t[2] == 'I':
            canon = t[1]
        elif t[0] == T_ID:
            canon = split_suffix(t[1])[0]
        else:
            self.err("RESTORE needs a label or line number")
        if canon not in self.data_at:
            self.err("unknown label '%s' in RESTORE" % t[1])
        self.emit('mm_restore(%d);' % self.data_at[canon])

    # -- SORT ---------------------------------------------------------------
    def do_sort(self):
        sym = self.arrayref()
        ptr, cnt = self.array_flat(sym)
        idx = 'NULL'
        flags = '0'
        start = str(self.opt_base)
        count = '-1'
        if self.accept_op(','):
            if not (self.is_op(',') or self.stmt_end()):
                isym = self.arrayref()
                if isym.ty != TY_I:
                    self.err("the SORT index array must be an integer array")
                idx = self.array_flat(isym)[0]
            if self.accept_op(','):
                if not (self.is_op(',') or self.stmt_end()):
                    flags = self.as_int(self.expr())
                if self.accept_op(','):
                    if not (self.is_op(',') or self.stmt_end()):
                        start = self.as_int(self.expr())
                    if self.accept_op(','):
                        if not self.stmt_end():
                            count = self.as_int(self.expr())
        kind = {TY_I: 'i', TY_F: 'f', TY_S: 's'}[sym.ty]
        self.uses_sort = True
        self.emit('mm_sort_%s(%s, %s, %s, (int)(%s), (int)(%s), (int)(%s));'
                  % (kind, ptr, idx, cnt, start, count, flags))

    def shortest(self, counts):
        """The smallest of some element counts, as a C expression.

        Textually identical counts collapse to one term, which is the
        usual case - the arrays are dimensioned together - so this
        normally emits no comparison at all."""
        seen = []
        for c in counts:
            if c not in seen:
                seen.append(c)
        e = seen[0]
        for c in seen[1:]:
            e = '((%s) < (%s) ? (%s) : (%s))' % (e, c, e, c)
        return e

    def do_pixels(self):
        """PIXEL xa(), ya() [, c | ca()] - MMBasic's array form.

        Draw.c cmd_pixel, the branch it takes when getargaddress reports
        more than one element.  One call for the whole run: a syscall
        costs 1.3us and a pixel store 15ns, so plotting point by point
        spends its time crossing into the kernel rather than drawing.

        One deviation from MMBasic, and only in a program that is
        already wrong: it takes the count from the Y array and clamps it
        to the colour array, so an X array shorter than Y is read past
        its end.  This takes the shortest of the three.  For arrays
        dimensioned together - every correct program - they agree."""
        xs = self.arrayref()
        xp, xn = self.array_flat(xs)
        self.expect_op(',')
        ys = self.arrayref()
        yp, yn = self.array_flat(ys)
        for s in (xs, ys):
            if s.ty == TY_S:
                self.err("PIXEL needs numeric coordinate arrays, and "
                         "'%s' is a string array" % s.name)
        cf, ci, rgb = 'NULL', 'NULL', 'MM_CUR'
        counts = [xn, yn]
        if self.accept_op(',') and not self.stmt_end():
            if self.is_array_arg():
                cs = self.arrayref()
                cp, cn = self.array_flat(cs)
                if cs.ty == TY_S:
                    self.err("the PIXEL colour array must be numeric, and "
                             "'%s' is a string array" % cs.name)
                if cs.ty == TY_F:
                    cf = cp
                else:
                    ci = cp
                counts.append(cn)
            else:
                # a single colour for the whole run - MMBasic's nc == 1
                rgb = self.as_int(self.expr())
        xf, xi = (xp, 'NULL') if xs.ty == TY_F else ('NULL', xp)
        yf, yi = (yp, 'NULL') if ys.ty == TY_F else ('NULL', yp)
        self.emit('mm_pixels(%s, %s, %s, %s, %s, %s, %s, %s);'
                  % (xf, xi, yf, yi, cf, ci, rgb, self.shortest(counts)))

    # -- INC / CAT / ERASE ---------------------------------------------------
    def do_inc(self):
        tgt, ty, cap = self.input_target()
        if self.accept_op(','):
            v = self.expr()
        else:
            v = ('1LL', TY_I)
        if ty == TY_S:
            if v[1] != TY_S:
                self.err("INC on a string needs a string increment")
            self.emit(self.swrite_cap(cap, tgt,
                                      'mm_scat(%s, %s)' % (tgt, v[0])))
            self.tmp_used = True
        elif ty == TY_I:
            self.emit('%s += %s;' % (tgt, self.as_int(v)))
        else:
            self.emit('%s += %s;' % (tgt, self.as_flt(v)))

    def do_cat(self):
        tgt, ty, cap = self.input_target()
        if ty != TY_S:
            self.err("CAT needs a string variable")
        self.expect_op(',')
        v = self.expr()
        if v[1] != TY_S:
            self.err("CAT needs a string to append")
        self.emit(self.swrite_cap(cap, tgt,
                                  'mm_scat(%s, %s)' % (tgt, v[0])))
        self.tmp_used = True

    def do_erase(self):
        warned = False
        while not self.stmt_end():
            t = self.peek()
            if t is None or t[0] != T_ID:
                self.err("ERASE needs a variable name")
            sym = self.reference(t[1], False)
            self.i += 1
            if self.accept_op('('):
                self.expect_op(')')
            if sym.is_array and sym.dynamic and not sym.is_param:
                # This one really is given back: its elements are on the
                # heap, so ERASE frees them and leaves it undimensioned,
                # exactly as the interpreter's erase() does.
                old = self.newtmp('ae')
                self.tmp_used = True
                self.emit('{ void *%s = %s;' % (old, sym.acc))
                self.emit('  %s = 0; %s[0] = 0;' % (sym.acc, sym.bacc))
                self.emit('  if (%s) mm_lfree(%s); }' % (old, old))
            else:
                if not warned:
                    self.warn("ERASE zeroes the variable; static storage "
                              "cannot be handed back the way the "
                              "interpreter does")
                    warned = True
                self.emit(self.zero_of(sym))
            if not self.accept_op(','):
                break

    def zero_of(self, sym):
        if sym.stype is not None:
            if sym.is_array:
                return 'memset(%s, 0, sizeof %s);' % (sym.acc, sym.acc)
            return 'memset(&%s, 0, sizeof %s);' % (sym.acc, sym.acc)
        if sym.is_array:
            ptr, cnt = self.array_flat(sym)
            self.uses_array = True
            if sym.ty == TY_S:
                return 'mm_arr_set_s(%s, %s, "\\000" "");' % (ptr, cnt)
            return 'mm_arr_set_%s(%s, %s, 0);' % (
                'i' if sym.ty == TY_I else 'f', ptr, cnt)
        if sym.ty == TY_S:
            return '%s[0] = 0; %s[1] = 0;' % (sym.acc, sym.acc)
        return '%s = 0;' % sym.acc

    # -- ON ERROR ------------------------------------------------------------
    def do_on_error(self):
        """ABORT | CLEAR | IGNORE | SKIP [n]  (cmd_on, Commands.c:8299).

        SKIP with no count is 2 and SKIP n is n+1, because the ON ERROR
        statement decrements the counter itself at its own end - so the
        count reaches the next statement intact."""
        w = self.peek()
        kw = w[2] if (w is not None and w[0] == T_ID) else ''
        if kw == 'RESTART':
            self.err("ON ERROR RESTART reboots the machine; a compiled "
                     "program has no equivalent")
        if kw not in ('ABORT', 'CLEAR', 'IGNORE', 'SKIP'):
            self.err("ON ERROR ABORT|CLEAR|IGNORE|SKIP [n]")
        self.i += 1
        mode = {'ABORT': 0, 'CLEAR': 1, 'IGNORE': 2, 'SKIP': 3}[kw]
        self.uses_onerror = True
        if kw == 'IGNORE':
            self.onerror_global = True
        nlit = None
        if kw == 'SKIP' and self.peek() is not None and not self.is_op(':'):
            w2 = self.peek()
            w3 = self.peek(1)
            if (w2[0] == T_NUM and w2[2] == 'I' and w2[1].isdigit()
                    and (w3 is None or (w3[0] == T_OP and w3[1] == ':'))):
                nlit = int(w2[1])
                n = w2[1]
                self.i += 1
            else:
                # the count is a run-time value: the window cannot be
                # laid out at compile time, so arm the whole program
                n = self.as_int(self.expr())
                self.onerror_global = True
        else:
            n = '1'
            if kw == 'SKIP':
                nlit = 1
        if nlit is not None:
            self.err_window_pending = nlit
        self.emit('mm_on_error(%d, %s);' % (mode, n))

    # -- ON nbr GOTO ---------------------------------------------------------
    def do_on_goto(self):
        v = self.expr()
        is_gosub = False
        if self.accept_kw('GOSUB'):
            is_gosub = True
        elif not self.accept_kw('GOTO'):
            self.err("ON <expr> GOTO|GOSUB label, label, ...")
        targets = []
        while True:
            t = self.nxt()
            if t[0] == T_NUM and t[2] == 'I':
                canon = t[1]
            elif t[0] == T_ID:
                canon = split_suffix(t[1])[0]
            else:
                self.err("ON ... GOTO needs labels")
            if canon not in self.labels:
                self.err("unknown label '%s'" % t[1])
            self.labels_used[canon] = 1
            self.note_goto(canon)
            targets.append(canon)
            if not self.accept_op(','):
                break
        if is_gosub:
            # each arm is its own GOSUB site so that RETURN lands correctly
            sel = self.newtmp('on')
            self.emit('{ int %s = (int)(%s);' % (sel, self.as_int(v)))
            self.indent += 1
            for k in range(len(targets)):
                self.emit('if (%s == %d) {' % (sel, k + 1))
                self.indent += 1
                self.emit_gosub(targets[k], targets[k])
                self.indent -= 1
                self.emit('}')
            self.indent -= 1
            self.emit('}')
            return
        self.emit('switch ((int)(%s)) {' % self.as_int(v))
        for k in range(len(targets)):
            self.emit('    case %d: goto %s;' % (k + 1, clabel(targets[k])))
        self.emit('    default: break;')
        self.emit('}')

    # -- ARRAY / MATH whole array commands -----------------------------------
    # C_MUL has two spellings in MMBasic and C_MULT is the older one;
    # cmd_math checks for both.  The value is the C operator.
    CCOMB = {'C_ADD': '+', 'C_SUB': '-', 'C_MUL': '*', 'C_MULT': '*',
             'C_DIV': '/', 'C_AND': '&', 'C_OR': '|', 'C_XOR': '^'}

    def do_array_cmd(self, is_math):
        t = self.nxt()
        if t[0] != T_ID:
            self.err("ARRAY/MATH needs a sub-command")
        op = t[2]
        if op in self.CCOMB:
            # MATH C_ADD a(), b(), c()   - c(i) = a(i) op b(i)
            #
            # MATH only: MMBasic has these in cmd_math and nowhere else,
            # so ARRAY C_ADD is a syntax error there and is one here.
            #
            # The loops live in mmb_math.h, included only by a program
            # that asks for one - the same bargain as the graphics and
            # pin headers.  Not the runtime: fourteen one-line loops
            # would be fourteen more wrappers in bcrun and fourteen
            # more names in its table, carried by every program on the
            # machine whether or not it says C_ADD.
            if not is_math:
                self.err("%s is a MATH sub-command, not an ARRAY one" % op)
            a = self.arrayref()
            self.expect_op(',')
            b = self.arrayref()
            self.expect_op(',')
            c = self.arrayref()
            if a.ty == TY_S or b.ty == TY_S or c.ty == TY_S:
                self.err("%s does not apply to a string array" % op)
            if a.ty != b.ty or b.ty != c.ty:
                self.err("%s needs all three arrays to be the same type"
                         % op)
            aptr, acnt = self.array_flat(a)
            bptr, bcnt = self.array_flat(b)
            cptr, ccnt = self.array_flat(c)
            self.uses_math = True
            self.emit("mmg_carr_%s(%s, %s, %s, %s, %s, %s, '%s');"
                      % ('i' if a.ty == TY_I else 'f',
                         aptr, acnt, bptr, bcnt, cptr, ccnt,
                         self.CCOMB[op]))
            return
        if op == 'SET':
            val = self.expr()
            self.expect_op(',')
            sym = self.arrayref()
            ptr, cnt = self.array_flat(sym)
            self.uses_array = True
            if sym.ty == TY_S:
                if val[1] != TY_S:
                    self.err("a string array needs a string value")
                self.emit('mm_arr_set_s(%s, %s, %s);' % (ptr, cnt, val[0]))
            elif sym.ty == TY_I:
                self.emit('mm_arr_set_i(%s, %s, %s);'
                          % (ptr, cnt, self.as_int(val)))
            else:
                self.emit('mm_arr_set_f(%s, %s, %s);'
                          % (ptr, cnt, self.as_flt(val)))
            return
        if op in ('ADD', 'SCALE'):
            # ARRAY ADD src(), v, dst()   and   MATH SCALE src(), v, dst()
            #
            # SCALE is MATH's alone: `ARRAY SCALE` answers "Unknown
            # command" on a real MMBasic, because cmd_array has no such
            # sub-command and only cmd_math does.
            if op == 'SCALE' and not is_math:
                self.err("SCALE is a MATH sub-command, not an ARRAY one")
            src = self.arrayref()
            self.expect_op(',')
            val = self.expr()
            self.expect_op(',')
            dst = self.arrayref()
            if src.ty != dst.ty:
                self.err("%s needs both arrays to be the same type" % op)
            sptr, scnt = self.array_flat(src)
            dptr, dcnt = self.array_flat(dst)
            self.uses_array = True
            if src.ty == TY_S:
                if op == 'SCALE':
                    self.err("SCALE does not apply to a string array")
                if val[1] != TY_S:
                    self.err("a string array needs a string value")
                self.emit('mm_arr_add_s(%s, %s, %s, %s, %s);'
                          % (sptr, scnt, val[0], dptr, dcnt))
                return
            fn = 'mm_arr_%s_%s' % (op.lower(),
                                   'i' if src.ty == TY_I else 'f')
            conv = self.as_int if src.ty == TY_I else self.as_flt
            self.emit('%s(%s, %s, %s, %s, %s);'
                      % (fn, sptr, scnt, conv(val), dptr, dcnt))
            return
        if op == 'POWER':
            # MATH POWER a(), n, b()  - b(i) = a(i) ^ n
            #
            # MATH only, as the C_ operations are: cmd_math has it and
            # cmd_array does not.  Shaped exactly like ADD and SCALE,
            # and refuses mixed types for the same reason they do.
            if not is_math:
                self.err("POWER is a MATH sub-command, not an ARRAY one")
            src = self.arrayref()
            self.expect_op(',')
            val = self.expr()
            self.expect_op(',')
            dst = self.arrayref()
            if src.ty == TY_S or dst.ty == TY_S:
                self.err("POWER does not apply to a string array")
            if src.ty != dst.ty:
                self.err("POWER needs both arrays to be the same type "
                         "(MMBasic converts between integer and float "
                         "here; this does not, as ARRAY ADD does not)")
            sptr, scnt = self.array_flat(src)
            dptr, dcnt = self.array_flat(dst)
            self.uses_math = True
            self.emit('mmg_pow_%s(%s, %s, %s, %s, %s);'
                      % ('i' if src.ty == TY_I else 'f',
                         sptr, scnt, self.as_flt(val), dptr, dcnt))
            return
        if op == 'SHIFT':
            # MATH SHIFT a%(), n, b%() [, "U"]
            #
            # Integer arrays only - "Argument 1 must be an integer
            # array" is MMBasic's own refusal, made here because the
            # types are known before the program runs.  The "U" is a
            # literal in the reference too (checkstring on argv[6]),
            # not an expression.
            if not is_math:
                self.err("SHIFT is a MATH sub-command, not an ARRAY one")
            src = self.arrayref()
            if src.ty != TY_I:
                self.err("Argument 1 must be an integer array")
            self.expect_op(',')
            n = self.as_int(self.expr())
            self.expect_op(',')
            dst = self.arrayref()
            if dst.ty != TY_I:
                self.err("Argument 3 must be an integer array")
            unsgn = '0'
            if self.accept_op(','):
                # BARE U, not "U" - checked on a real MMBasic, where
                # the quoted form silently gives the ARITHMETIC shift:
                # checkstring compares the argument's own text, and the
                # quotes are part of it.  That is a wrong answer rather
                # than an error there, so this refuses it instead of
                # copying the silence.
                if not self.accept_kw('U'):
                    self.err("MATH SHIFT's fourth argument is U, "
                             'unquoted (a real MMBasic ignores "U" and '
                             'shifts arithmetically)')
                unsgn = '1'
            sptr, scnt = self.array_flat(src)
            dptr, dcnt = self.array_flat(dst)
            self.uses_math = True
            self.emit('mmg_shift(%s, %s, %s, %s, %s, %s);'
                      % (sptr, scnt, n, dptr, dcnt, unsgn))
            return
        if op in ('V_NORMALISE', 'V_CROSS'):
            # MATH V_NORMALISE a(), b()      b = a / |a|
            # MATH V_CROSS     a(), b(), c() c = a x b
            #
            # One-dimensional float arrays: parsefloatarray takes 1 for
            # its dimension count in both.  array_line enforces the
            # rank and hands back the count MMBasic counts - the
            # REACHABLE elements, which is one fewer per dimension than
            # ours under OPTION BASE 1.
            if not is_math:
                self.err("%s is a MATH sub-command, not an ARRAY one" % op)
            arrs = [self.arrayref()]
            while self.accept_op(','):
                arrs.append(self.arrayref())
            want = 2 if op == 'V_NORMALISE' else 3
            if len(arrs) != want:
                self.err("MATH %s takes %d arrays" % (op, want))
            for a in arrs:
                if a.ty != TY_F:
                    self.err("Argument %d must be a floating point array"
                             % (arrs.index(a) + 1))
            parts = []
            for a in arrs:
                parts.extend(self.array_line(a))
            self.uses_math = True
            self.emit('%s(%s);'
                      % ('mmg_vnorm' if op == 'V_NORMALISE' else 'mmg_vcross',
                         ', '.join(parts)))
            return
        if op == 'WINDOW':
            # MATH WINDOW in(), outmin, outmax, out() [, minvar, maxvar]
            #
            # Any rank, and any mix of float and integer between in and
            # out - which is the point of the statement rather than an
            # accident, so all four combinations are here where ARRAY
            # ADD refuses them.  The last two are both or neither, and
            # receive the INPUT's own range.
            if not is_math:
                self.err("WINDOW is a MATH sub-command, not an ARRAY one")
            src = self.arrayref()
            self.expect_op(',')
            omin = self.as_flt(self.expr())
            self.expect_op(',')
            omax = self.as_flt(self.expr())
            self.expect_op(',')
            dst = self.arrayref()
            if src.ty == TY_S or dst.ty == TY_S:
                self.err("MATH WINDOW needs numeric arrays")
            lo = hi = None
            if self.accept_op(','):
                lo = self.window_var()
                self.expect_op(',')
                hi = self.window_var()
            sptr, scnt = self.array_flat(src)
            dptr, dcnt = self.array_flat(dst)
            fn = 'mmg_window_%s%s' % ('i' if src.ty == TY_I else 'f',
                                      'i' if dst.ty == TY_I else 'f')
            self.uses_math = True
            if lo is None:
                self.emit('%s(%s, %s, %s, %s, %s, %s, NULL, NULL);'
                          % (fn, sptr, scnt, omin, omax, dptr, dcnt))
                return
            a = self.newtmp('wlo')
            b = self.newtmp('whi')
            self.tmp_used = True
            self.emit('{ MMFLOAT %s, %s;' % (a, b))
            self.emit('  %s(%s, %s, %s, %s, %s, %s, &%s, &%s);'
                      % (fn, sptr, scnt, omin, omax, dptr, dcnt, a, b))
            # An INTEGER target truncates, as the reference's
            # (long long int) cast does - not mm_toint, which rounds.
            for tgt, tmp in ((lo, a), (hi, b)):
                acc, ty = tgt
                self.emit('  %s = %s;'
                          % (acc, tmp if ty == TY_F
                             else '(MMINTEGER)%s' % tmp))
            self.emit('}')
            return
        if op in ('Q_INVERT', 'Q_MULT', 'Q_ROTATE'):
            # MATH Q_INVERT q(), n()
            # MATH Q_MULT   q1(), q2(), n()
            # MATH Q_ROTATE q(), v(), n()
            #
            # A quaternion is FIVE floats - w, x, y, z and the
            # magnitude taken out of them - so every array here is
            # one-dimensional and exactly five long.  The length is
            # checked in the header rather than here because a
            # run-time DIM has no length until then; for a DIMmed
            # array the count is a constant and cc1 folds the test
            # away.
            if not is_math:
                self.err("%s is a MATH sub-command, not an ARRAY one" % op)
            want = 2 if op == 'Q_INVERT' else 3
            arrs = [self.arrayref()]
            while self.accept_op(','):
                arrs.append(self.arrayref())
            if len(arrs) != want:
                self.err("MATH %s takes %d arrays" % (op, want))
            parts = []
            for k, a in enumerate(arrs):
                if a.ty != TY_F:
                    self.err("Argument %d must be a 5 element floating "
                             "point array" % (k + 1))
                parts.extend(self.array_line(a))
            self.uses_math = True
            fn = {'Q_INVERT': 'mmg_q_invert', 'Q_MULT': 'mmg_q_mult',
                  'Q_ROTATE': 'mmg_q_rotate'}[op]
            self.emit('%s(%s);' % (fn, ', '.join(parts)))
            return
        if op in ('Q_CREATE', 'Q_EULER', 'Q_VECTOR'):
            # MATH Q_CREATE theta, x, y, z, q()
            # MATH Q_EULER  yaw, pitch, roll, q()
            # MATH Q_VECTOR x, y, z, q()
            #
            # OPTION ANGLE applies to the angles of the first two, as
            # it does to SIN and COS.  Q_CREATE halves theta on the way
            # in - cmd_math writes `theta / 2.0 / optionangle`, one
            # division done once - and Q_EULER NEGATES the yaw, which
            # is the reference's convention rather than a slip.
            if not is_math:
                self.err("%s is a MATH sub-command, not an ARRAY one" % op)
            nnum = 4 if op == 'Q_CREATE' else 3
            nums = [self.as_flt(self.expr())]
            while len(nums) < nnum:
                self.expect_op(',')
                nums.append(self.as_flt(self.expr()))
            self.expect_op(',')
            q = self.arrayref()
            if q.ty != TY_F:
                self.err("Argument 4 must be a 5 element floating point "
                         "array")

            def ang(v, neg=False):
                if neg:
                    v = '(-(%s))' % v
                if self.opt_angle:
                    return '((%s) / %s)' % (v, self.opt_angle)
                return v

            if op == 'Q_CREATE':
                nums[0] = ang('(%s) / 2.0' % nums[0])
                fn = 'mmg_q_create'
            elif op == 'Q_EULER':
                nums[0] = ang(nums[0], True)
                nums[1] = ang(nums[1])
                nums[2] = ang(nums[2])
                fn = 'mmg_q_euler'
            else:
                fn = 'mmg_q_vector'
            qp, qn = self.array_line(q)
            self.uses_math = True
            self.emit('%s(%s, %s, %s);' % (fn, ', '.join(nums), qp, qn))
            return
        if op in ('M_TRANSPOSE', 'M_MULT', 'M_INVERSE'):
            # MATH M_TRANSPOSE a(), b()
            # MATH M_MULT     a(), b(), c()
            # MATH M_INVERSE  a(), b()
            #
            # Two-dimensional float arrays throughout, which is
            # parsefloatarray's own restriction here (a dimension count
            # of 2 in every one of them).  array_plane hands back
            # MMBasic's own pair of names - dims[0] is the COLUMN count
            # and dims[1] the row count - and the shape rules are
            # checked at run time because a run-time DIM has no shape
            # until then.
            if not is_math:
                self.err("%s is a MATH sub-command, not an ARRAY one" % op)
            want = 3 if op == 'M_MULT' else 2
            arrs = [self.arrayref()]
            while self.accept_op(','):
                arrs.append(self.arrayref())
            if len(arrs) != want:
                self.err("MATH %s takes %d arrays" % (op, want))
            parts = []
            for k, a in enumerate(arrs):
                if a.ty != TY_F:
                    self.err("Argument %d must be a floating point array"
                             % (k + 1))
                p = self.array_plane(a)
                parts.extend([p[0], p[1], p[2]])
            self.uses_math = True
            fn = {'M_TRANSPOSE': 'mmg_mtrans', 'M_MULT': 'mmg_mmult',
                  'M_INVERSE': 'mmg_minv'}[op]
            self.emit('%s(%s);' % (fn, ', '.join(parts)))
            return
        if op == 'V_MULT':
            # MATH V_MULT a(), b(), c()  - a matrix by a vector
            #
            # a is two-dimensional; b and c are one-dimensional, b as
            # long as a's COLUMN count and c as long as its row count.
            if not is_math:
                self.err("V_MULT is a MATH sub-command, not an ARRAY one")
            m = self.arrayref()
            self.expect_op(',')
            v = self.arrayref()
            self.expect_op(',')
            o = self.arrayref()
            for k, a in enumerate((m, v, o)):
                if a.ty != TY_F:
                    self.err("Argument %d must be a floating point array"
                             % (k + 1))
            mp = self.array_plane(m)
            vp, vn = self.array_line(v)
            op_, on = self.array_line(o)
            self.uses_math = True
            self.emit('mmg_vmult(%s, %s, %s, %s, %s, %s, %s);'
                      % (mp[0], mp[1], mp[2], vp, vn, op_, on))
            return
        if op == 'V_ROTATE':
            # MATH V_ROTATE xo, yo, angle, xin(), yin(), xout(), yout()
            #
            # Four one-dimensional arrays of one type.  The angle goes
            # in divided by OPTION ANGLE's multiplier, as SIN and COS
            # do - cmd_math divides by `optionangle` before the cos and
            # sin, and this is the same division at the same place.
            if not is_math:
                self.err("V_ROTATE is a MATH sub-command, not an ARRAY one")
            ox = self.as_flt(self.expr())
            self.expect_op(',')
            oy = self.as_flt(self.expr())
            self.expect_op(',')
            ang = self.as_flt(self.expr())
            if self.opt_angle:
                ang = '((%s) / %s)' % (ang, self.opt_angle)
            arrs = []
            while self.accept_op(','):
                arrs.append(self.arrayref())
            if len(arrs) != 4:
                self.err("MATH V_ROTATE takes four arrays: xin(), "
                         "yin(), xout(), yout()")
            ty = arrs[0].ty
            if ty == TY_S:
                self.err("MATH V_ROTATE needs numeric arrays")
            for k, a in enumerate(arrs):
                if a.ty != ty:
                    self.err("MATH V_ROTATE needs all four arrays to be "
                             "the same type (MMBasic takes any mix; this "
                             "does not)")
            parts = [ox, oy, ang]
            for a in arrs:
                parts.extend(self.array_line(a))
            self.uses_math = True
            self.emit('mmg_vrotate%s(%s);'
                      % ('_i' if ty == TY_I else '', ', '.join(parts)))
            return
        if op in ('V_PRINT', 'M_PRINT'):
            # MATH V_PRINT a() [, HEX]   one line
            # MATH M_PRINT a()           one line per row
            #
            # HEX is a literal, and only for an integer array: the
            # reference raises "Trying to print a float in HEX" at run
            # time, which is a translate-time refusal here.
            if not is_math:
                self.err("%s is a MATH sub-command, not an ARRAY one" % op)
            sym = self.arrayref()
            if sym.ty == TY_S:
                self.err("MATH %s needs a numeric array" % op)
            hexed = False
            if self.accept_op(','):
                h = self.nxt()
                if h[0] != T_ID or h[2] != 'HEX':
                    self.err("MATH %s's second argument is HEX" % op)
                if op == 'M_PRINT':
                    self.err("MATH M_PRINT takes one argument")
                if sym.ty == TY_F:
                    self.err("Trying to print a float in HEX")
                hexed = True
            self.uses_math = True
            sfx = 'i' if sym.ty == TY_I else 'f'
            if op == 'V_PRINT':
                ptr, cnt = self.array_line(sym)
                if sym.ty == TY_I:
                    self.emit('mmg_vprint_i(%s, %s, %d);'
                              % (ptr, cnt, 16 if hexed else 10))
                else:
                    self.emit('mmg_vprint_f(%s, %s);' % (ptr, cnt))
                return
            ptr, nc, nr, stride = self.array_plane(sym)
            self.emit('mmg_mprint_%s(%s, %s, %s, %s);'
                      % (sfx, ptr, nc, nr, stride))
            return
        if op in ('SLICE', 'INSERT'):
            # ARRAY SLICE  from(), i1, , i3, to()     - read one line out
            # ARRAY INSERT into(), i1, , i3, from()   - write one line in
            #
            # MATH SLICE and MATH INSERT are the same two commands:
            # MMBasic's cmd_math calls array_slice and array_insert, the
            # very functions cmd_slice and cmd_insert call.
            #
            # Exactly one index is left blank, and that is the dimension
            # the line runs along.  The blank is a comma with nothing
            # before it, so it is recognised by finding a comma where an
            # expression should have started - and the array at the end
            # is recognised the way PIXEL recognises its array form, by
            # the a() spelling.
            arr = self.arrayref()
            self.expect_op(',')
            parts = []
            blank = None
            while not self.is_array_arg():
                if self.is_op(','):
                    if blank is not None:
                        self.err("ARRAY %s: only one index can be omitted"
                                 % op)
                    blank = len(parts)
                    parts.append(None)
                else:
                    parts.append('(int)(%s)' % self.as_int(self.expr()))
                if not self.accept_op(','):
                    self.err("ARRAY %s wants the one-dimensional array "
                             "last, written b()" % op)
            line = self.arrayref()
            if blank is None:
                self.err("ARRAY %s: leave one index blank to say which "
                         "dimension the line runs along" % op)
            if arr.ty != line.ty:
                self.err("ARRAY %s needs both arrays to be the same type "
                         "(MMBasic converts between integer and float "
                         "here; this does not, as ARRAY ADD does not)"
                         % op)
            ptr, step, n = self.array_vector(arr, parts, blank)
            lptr, lcnt = self.array_line(line)
            sfx = {TY_I: 'i', TY_F: 'f', TY_S: 's'}[arr.ty]
            self.uses_array = True
            if op == 'SLICE':
                self.emit('mm_arr_copy_%s(%s, 1, %s, %s, %s, %s);'
                          % (sfx, lptr, ptr, step, n, lcnt))
            else:
                self.emit('mm_arr_copy_%s(%s, %s, %s, 1, %s, %s);'
                          % (sfx, ptr, step, lptr, n, lcnt))
            return
        if op == 'RANDOMIZE':
            if self.stmt_end():
                self.uses_datetime = True
                self.emit('mm_randomize(mm_epoch_now());')
            else:
                self.emit('mm_randomize(%s);' % self.as_int(self.expr()))
            return
        self.err("MATH/ARRAY %s is not supported" % t[1])

    # -- files ------------------------------------------------------------
    def do_open(self):
        name = self.expr()
        if name[1] != TY_S:
            self.err("OPEN needs a file name string")
        if not self.accept_kw('FOR'):
            self.err("serial ports (OPEN comspec$ AS #n) are not supported")
        t = self.nxt()
        if t[0] != T_ID or t[2] not in ('INPUT', 'OUTPUT', 'APPEND', 'RANDOM'):
            self.err("OPEN mode must be INPUT, OUTPUT, APPEND or RANDOM")
        mode = 'MM_F_' + t[2]
        if not self.accept_kw('AS'):
            self.err("OPEN ... FOR ... AS #n")
        fn = self.channel()
        self.emit('mm_open(%s, %s, %s);' % (name[0], mode, fn))

    def do_close(self):
        while not self.stmt_end():
            fn = self.channel()
            self.emit('mm_close(%s);' % fn)
            if not self.accept_op(','):
                break

    def do_fileword(self, up):
        if up == 'FILES':
            if self.stmt_end():
                self.emit('mm_files("\\000" "");')
                return
            v = self.expr()
            if v[1] != TY_S:
                self.err("FILES needs a file specification string")
            self.emit('mm_files(%s);' % v[0])
            self.skip_statement()        # an optional sort order
            return
        v = self.expr()
        if v[1] != TY_S:
            self.err("%s needs a string" % up)
        self.emit('mm_%s(%s);' % (up.lower(), v[0]))
        self.skip_statement()            # KILL's optional 'all'

    def input_target(self):
        """A variable, possibly an array element, that INPUT can write.

        Returns (accessor, type, capacity), where the capacity is the
        LENGTH of a string array element and None for everything else -
        a plain string has room for its NUL and takes mm_sset."""
        t = self.nxt()
        if t[0] != T_ID:
            self.err("INPUT needs a variable")
        canon, sfx = split_suffix(t[1])
        # INSIDE A FUNCTION, ITS OWN NAME IS A VARIABLE - the return
        # value - and CAT, INC and INPUT write it like any other.
        # Without this the write went to an invisible implied GLOBAL of
        # the same name: PETSCII Robots' path$() builds its result with
        # `Cat path$, "/" + f$`, so every file path came back as the
        # bare directory and loadimage read a DIRECTORY as its BMP -
        # "not a BMP file" with a file that was perfectly good.
        if (self.cur is not None and self.cur.is_func
                and canon == self.cur.name and not self.is_op('(')):
            if sfx is not None and sfx != self.cur.ty:
                self.err("'%s' is %s but used as %s"
                         % (canon, TYNAME[self.cur.ty], TYNAME[sfx]))
            return (self.retacc(), self.cur.ty, None)
        is_arr = self.is_op('(')
        sym = self.reference(t[1], False)
        if sym.is_const:
            self.err("'%s' is a CONST" % sym.name)
        if is_arr:
            if not sym.is_array:
                self.err("'%s' is not an array" % sym.name)
            return (self.index(sym), sym.ty, sym.slen)
        if sym.is_array:
            self.err("cannot INPUT into a whole array")
        return (sym.acc, sym.ty, None)

    def do_input(self):
        chan = '0'
        if self.is_op('#'):
            chan = self.channel()
            self.accept_op(',')
        else:
            t = self.peek()
            if t is not None and t[0] == T_STR \
                    and (self.is_op(';', 1) or self.is_op(',', 1)):
                self.i += 1
                self.emit('mm_pr_s(%s);' % c_string_literal(t[1]))
                if self.is_op(';'):
                    self.emit('mm_pr_s("\\002" "? ");')
                self.i += 1
            else:
                self.emit('mm_pr_s("\\002" "? ");')
        self.emit('mm_input_line(%s);' % chan)
        while not self.stmt_end():
            tgt, ty, cap = self.input_target()
            if ty == TY_S:
                self.emit(self.swrite_cap(cap, tgt, 'mm_input_next()'))
            elif ty == TY_I:
                self.emit('%s = mm_atoi(mm_input_next());' % tgt)
            else:
                self.emit('%s = mm_atof(mm_input_next());' % tgt)
            self.tmp_used = True
            if not self.accept_op(','):
                break

    def do_line_input(self):
        chan = '0'
        if self.is_op('#'):
            chan = self.channel()
            self.accept_op(',')
        else:
            t = self.peek()
            if t is not None and t[0] == T_STR \
                    and (self.is_op(',', 1) or self.is_op(';', 1)):
                self.i += 1
                self.emit('mm_pr_s(%s);' % c_string_literal(t[1]))
                self.i += 1
        tgt, ty, cap = self.input_target()
        if ty != TY_S:
            self.err("LINE INPUT needs a string variable")
        self.emit(self.swrite_cap(cap, tgt, 'mm_getline(%s)' % chan))
        self.tmp_used = True

    # -- assignment / sub call -------------------------------------------
    def looks_like_assignment(self):
        """Scan ahead over an optional bracketed index for a top level '='."""
        j = self.i
        if j >= len(self.toks) or self.toks[j][0] != T_ID:
            return False
        j += 1
        if j < len(self.toks) and self.toks[j][0] == T_OP \
                and self.toks[j][1] == '(':
            depth = 0
            while j < len(self.toks):
                tk = self.toks[j]
                if tk[0] == T_OP and tk[1] == '(':
                    depth += 1
                elif tk[0] == T_OP and tk[1] == ')':
                    depth -= 1
                    if depth == 0:
                        j += 1
                        break
                j += 1
        return (j < len(self.toks) and self.toks[j][0] == T_OP
                and self.toks[j][1] == '=')

    def do_assign_or_call(self):
        t = self.peek()
        canon, sfx = split_suffix(t[1])
        if t[2] == 'MID$' and not self.looks_like_assignment():
            # MID$(s$, n, m) = x$   -- looks_like_assignment cannot see it
            pass
        if canon in self.routines and not self.looks_like_assignment():
            self.i += 1
            r = self.routines[canon]
            args = self.call_args(False)
            code, ty = self.emit_call(r, args)
            self.emit('(void)(%s);' % code if r.is_func else '%s;' % code)
            return
        if t[2] == 'MID$':
            self.do_mid_assign()
            return
        self.do_assign()

    def do_blit_memform(self):
        """MEMORY|COMPRESSED addr, x, y [, t] - shared by BLIT and
        SPRITE: on an LCD PicoMite the two are one command."""
        is_mem = self.is_kw('MEMORY', 1)
        self.i += 2
        a = self.as_int(self.expr())
        self.expect_op(',')
        x = self.as_int(self.expr())
        self.expect_op(',')
        y = self.as_int(self.expr())
        blank = '-1LL'
        if self.accept_op(','):
            blank = self.as_int(self.expr())
        self.emit('mmb_blit_%s(%s, %s, %s, %s);'
                  % ('mem' if is_mem else 'comp', a, x, y, blank))

    def do_callstmt(self):
        t = self.peek()
        if t is not None and t[0] == T_STR:
            # a literal name resolves here and now: a direct call
            self.i += 1
            canon = split_suffix(t[1].lower())[0]
            r = self.routines.get(canon)
            if r is None:
                self.err("CALL to unknown subroutine '%s'" % canon)
            self.accept_op(',')
            args = self.call_args(False)
            code, ty = self.emit_call(r, args)
            self.emit('%s;' % code)
            return
        if t is not None and t[0] == T_ID \
                and split_suffix(t[1])[0] in self.routines:
            # the classic form: CALL subname [, args]
            self.i += 1
            r = self.routines[split_suffix(t[1])[0]]
            self.accept_op(',')
            args = self.call_args(False)
            code, ty = self.emit_call(r, args)
            self.emit('%s;' % code)
            return
        # the name is a run-time string: dispatch by name
        v = self.expr()
        if v[1] != TY_S:
            self.err("CALL needs a SUB name or a string")
        args = []
        if self.accept_op(','):
            while True:
                args.append(self.arg_item())
                if not self.accept_op(','):
                    break
        d = self.call_dispatch(False, args)
        code, _ = self.emit_call_byname(d, v[0], args)
        self.emit(code + ';')

    def do_mid_assign(self):
        self.i += 1
        self.expect_op('(')
        tgt = self.lvalue_from_here()
        self.expect_op(',')
        start = self.expr()
        num = None
        if self.accept_op(','):
            num = self.expr()
        self.expect_op(')')
        self.expect_op('=')
        v = self.expr()
        if v[1] != TY_S:
            self.err("MID$() assignment needs a string")
        self.emit('mm_mid_assign(%s, %s, %s, %s);'
                  % (tgt, self.as_int(start),
                     self.as_int(num) if num else '-1LL', v[0]))

    def lvalue_from_here(self):
        t = self.nxt()
        if t[0] != T_ID:
            self.err("variable expected")
        s = self.reference(t[1], self.is_op('('))
        if s.is_array:
            return self.index(s)
        return s.acc

    def do_assign(self):
        t = self.nxt()
        if t[0] != T_ID:
            self.err("assignment target expected")
        canon, sfx = split_suffix(t[1])

        # assignment to the enclosing function's name = set return value
        if self.cur is not None and self.cur.is_func and canon == self.cur.name:
            self.expect_op('=')
            v = self.expr()
            ty = self.cur.ty
            if ty == TY_S:
                if v[1] != TY_S:
                    self.err("function '%s' returns a string" % canon)
                self.emit('mm_sset(__ret, %s);' % v[0])
            elif ty == TY_I:
                self.store('__ret', self.as_int(v), TY_I)
            else:
                self.store('__ret', self.as_flt(v), TY_F)
            return

        sh = self.struct_head(t[1])
        if sh is not None:
            s2, parts, sfx2 = sh
            base = self.struct_base(s2)
            self.assign_member(self.member_path(base, s2.stype, parts,
                                                sfx2))
            return

        is_arr = self.is_op('(')
        s = self.reference(t[1], False)
        if s.stype is not None:
            if is_arr:
                if not s.is_array:
                    self.err("'%s' is not an array" % canon)
                target = self.index(s)
            else:
                if s.is_array:
                    self.err("cannot assign to whole struct array '%s'"
                             % canon)
                target = s.acc
            if self.is_op('.'):
                self.assign_member(self.member_path(target, s.stype,
                                                    [], None))
                return
            self.expect_op('=')
            self.assign_struct(target, s.stype)
            return
        if is_arr:
            if not s.is_array:
                self.err("'%s' is not an array" % canon)
            target = self.index(s)
        else:
            if s.is_array:
                self.err("cannot assign to whole array '%s'" % canon)
            target = s.acc
        if s.is_const:
            self.err("'%s' is a CONST and cannot be assigned to" % canon)
        self.expect_op('=')
        v = self.expr()
        if s.ty == TY_S:
            if v[1] != TY_S:
                self.err("cannot assign a number to string '%s'" % canon)
            self.emit(self.swrite(s, target, v[0]))
        elif s.ty == TY_I:
            self.store(target, self.as_int(v), TY_I)
        else:
            self.store(target, self.as_flt(v), TY_F)

    def assign_member(self, res):
        """... = expr  where the target is a structure member."""
        self.expect_op('=')
        if res[0] == 'num':
            v = self.expr()
            if res[2] == TY_I:
                self.emit('%s = %s;' % (res[1], self.as_int(v)))
            else:
                self.emit('%s = %s;' % (res[1], self.as_flt(v)))
            return
        if res[0] == 'str':
            v = self.expr()
            if v[1] != TY_S:
                self.err("cannot assign a number to a string member")
            # bounded, and no trailing NUL when full: a member string
            # is LENGTH+1 bytes in the firmware's layout and the byte
            # after it belongs to the next member
            self.emit('mm_ssetm(%s, %d, %s);' % (res[1], res[2], v[0]))
            return
        # a whole nested structure: the firmware memcpy's the OUTER
        # type's size here and overruns - refused, not reproduced
        self.err("assigning a whole structure into a member is not "
                 "supported (the firmware overruns memory here); "
                 "assign the member's own members instead")

    def assign_struct(self, target, tyname):
        """target = <struct lvalue> - whole-structure copy."""
        v = self.expr()
        ty = v[1]
        if not isinstance(ty, tuple):
            self.err("a structure can only be assigned a structure of "
                     "the same TYPE")
        if ty[0] == 'TM':
            self.err("assigning a whole structure from a nested member "
                     "is not supported (the firmware over-reads "
                     "memory here)")
        if ty[1] != tyname:
            self.err("structure types must match (TYPE '%s' vs '%s')"
                     % (tyname, ty[1]))
        self.emit('%s = %s;' % (target, v[0]))

    def struct_operand(self):
        """A STRUCT-verb operand: v, arr(i) or arr().  Returns
        (kind, code, sym) with kind 'one' or 'all'."""
        t = self.nxt()
        if t[0] != T_ID:
            self.err("structure variable expected")
        canon, sfx = split_suffix(t[1])
        if '.' in canon:
            self.err("STRUCT works on whole structures, not members")
        s = self.lookup(canon)
        if s is None or s.stype is None:
            self.err("'%s' is not a structure variable" % t[1])
        self.note_touch(canon, s)
        if s.is_array:
            self.expect_op('(')
            if self.accept_op(')'):
                return ('all', s.acc, s)
            first = self.as_int(self.expr())
            parts = ['(int)(%s)' % first]
            while self.accept_op(','):
                parts.append('(int)(%s)' % self.as_int(self.expr()))
            self.expect_op(')')
            if len(parts) != len(s.dims):
                self.err("'%s' has %d dimension(s)" % (canon,
                                                       len(s.dims)))
            return ('one',
                    s.acc + ''.join('[' + p + ']' for p in parts), s)
        return ('one', s.acc, s)

    def do_struct(self):
        """STRUCT COPY|CLEAR|SWAP - the rest of the verbs need the
        interpreter's machinery or a raw-file runtime entry and are
        refused for now."""
        t = self.nxt()
        verb = t[2] if t[0] == T_ID else ''
        if verb == 'COPY':
            src = self.struct_operand()
            if not self.accept_kw('TO'):
                self.err("STRUCT COPY src TO dst")
            dst = self.struct_operand()
            if src[2].stype != dst[2].stype:
                self.err("structure types must match")
            if src[0] != dst[0]:
                self.err("both operands must be arrays or both single "
                         "structures")
            if src[0] == 'all':
                self.emit('memcpy(%s, %s, sizeof %s);'
                          % (dst[1], src[1], src[1]))
            else:
                self.emit('%s = %s;' % (dst[1], src[1]))
            return
        if verb == 'CLEAR':
            op = self.struct_operand()
            if op[0] == 'all':
                self.emit('memset(%s, 0, sizeof %s);' % (op[1], op[1]))
            else:
                self.emit('memset(&%s, 0, sizeof %s);' % (op[1], op[1]))
            return
        if verb == 'SWAP':
            a = self.struct_operand()
            self.expect_op(',')
            b = self.struct_operand()
            if a[2].stype != b[2].stype:
                self.err("structure types must match")
            if a[0] == 'all' or b[0] == 'all':
                self.err("STRUCT SWAP takes single structures")
            # no initialised declaration: the fcc front end takes
            # struct assignment but not struct initialisers
            self.emit('{ struct t_%s __ts; __ts = %s; %s = %s; '
                      '%s = __ts; }'
                      % (a[2].stype, a[1], a[1], b[1], b[1]))
            return
        if verb in ('SORT', 'SAVE', 'LOAD', 'PRINT', 'EXTRACT',
                    'INSERT'):
            self.err("STRUCT %s is not translated yet" % verb)
        self.err("unknown STRUCT subcommand '%s'" % t[1])

    def struct_initialiser(self, s):
        """DIM v AS T = (v1, v2, ...) - values flattened in member
        order.  Emitted as ordinary member assignments; the firmware
        does not length-check string values here, this does."""
        if s is not None and s.is_array:
            self.err("an initialiser on a struct ARRAY is not "
                     "translated yet")
        self.expect_op('(')
        td = self.types[s.stype] if s is not None else None
        k = 0
        while True:
            if td is None or k >= len(td.members):
                self.err("too many initialisation values")
            m = td.members[k]
            k += 1
            if m.stype is not None:
                self.err("a nested-struct member cannot appear in an "
                         "initialiser (the firmware rejects it too)")
            n = m.count
            for e in range(n):
                v = self.expr()
                if self.mode == 'emit':
                    code = '%s.m_%s' % (s.acc, m.name)
                    if m.ty == TY_S:
                        if m.dims is not None:
                            code = '(%s + %d)' % (code, e * (m.slen + 1))
                        self.emit('mm_ssetm(%s, %d, %s);'
                                  % (code, m.slen, self.as_str(v)))
                    else:
                        if m.dims is not None:
                            code += '[%d]' % e
                        self.emit('%s = %s;'
                                  % (code, self.as_int(v) if m.ty == TY_I
                                     else self.as_flt(v)))
                if e < n - 1:
                    self.expect_op(',')
            if not self.accept_op(','):
                break
        self.expect_op(')')
        if td is not None and k < len(td.members):
            self.err("not enough initialisation values for TYPE '%s'"
                     % td.disp)

    # -- IF ---------------------------------------------------------------
    def cond(self):
        v = self.expr()
        if v[1] == TY_S:
            self.err("a string cannot be used as a condition")
        # a comparison is already a truth value: wrapping it in '!= 0'
        # made the backend compare the compare, every time the
        # condition ran
        if boolean_expr(v[0]):
            return v[0]
        return '(%s) != 0' % v[0]

    def poisoned_cond(self, c, enter):
        """What a block header does when its own condition raised.

        The interpreter resumes at the textually next statement, so the
        answer depends on the form the translator is looking at, which is
        the one thing it knows and the interpreter does not have to: for a
        multi-line IF or a loop the next statement is inside the body, so
        it is entered; for a single-line IF the next statement is the next
        line, so the whole statement is skipped."""
        if not self.checks_on():
            return c
        if enter:
            return '__mm_e[0] ? 1 : (%s)' % c
        return '!__mm_e[0] && (%s)' % c

    def do_if(self):
        c = self.cond()
        if not self.accept_kw('THEN'):
            if self.is_kw('GOTO'):
                pass
            else:
                self.err("IF without THEN")
        if self.stmt_end():
            # block IF.  A condition that failed leaves the interpreter
            # resuming at the textually next statement - which for a
            # MULTI-LINE IF is the first statement of the THEN body.  So
            # the poisoned condition is TRUE here, and false below.
            self.emit('if (%s) {' % self.poisoned_cond(c, True))
            self.indent += 1
            self.blocks.append(['if', self.lineno])
            return
        # single line IF: the next statement is the next LINE, so a failed
        # condition skips the whole thing
        self.emit('if (%s) {' % self.poisoned_cond(c, False))
        self.indent += 1
        if self.is_kw('GOTO'):
            self.i += 1
            self.do_goto()
        elif self.peek() is not None and self.peek()[0] == T_NUM:
            self.do_goto()          # IF expr THEN <line number>
        else:
            self.inline_statements()
        self.indent -= 1
        if self.accept_kw('ELSE'):
            self.emit('} else {')
            self.indent += 1
            self.inline_statements()
            self.indent -= 1
        self.emit('}')

    def inline_statements(self):
        depth = len(self.blocks)
        self.inline += 1
        try:
            while not self.at_end() and not self.is_kw('ELSE'):
                if self.accept_op(':'):
                    continue
                self.statement()
                if len(self.blocks) != depth:
                    self.err("a single line IF cannot open a multi-line block")
        finally:
            self.inline -= 1

    def do_elseif(self):
        if not self.blocks or self.blocks[-1][0] != 'if':
            self.err("ELSEIF without IF")
        c = self.cond()
        self.accept_kw('THEN')
        self.indent -= 1
        self.emit('} else if (%s) {' % c)
        self.indent += 1

    def do_else(self):
        if not self.blocks or self.blocks[-1][0] != 'if':
            self.err("ELSE without IF")
        self.indent -= 1
        self.emit('} else {')
        self.indent += 1

    def close_block(self, kind):
        if not self.blocks or self.blocks[-1][0] != kind:
            # A block whose OPENER could not be translated leaves its
            # close with nothing to match, and "mismatched end of if
            # block" then reads as a second, separate fault in a line
            # that is perfectly good.  Say which line actually caused it
            # - one real error and one consequence, not two mysteries.
            if self.skipped:
                self.err("end of %s block with no start - line %d above "
                         "could not be translated" % (kind,
                                                      self.skipped[-1][0]))
            self.err("mismatched end of %s block" % kind)
        blk = self.blocks.pop()
        self.indent -= 1
        self.emit('}')
        return blk

    # -- FOR --------------------------------------------------------------
    def do_for(self):
        t = self.nxt()
        if t[0] != T_ID:
            self.err("FOR needs a counter variable")
        canon, sfx = split_suffix(t[1])
        s = self.reference(t[1], False)
        if s.ty == TY_S:
            self.err("FOR counter cannot be a string")
        if s.is_array:
            self.err("FOR counter cannot be an array")
        self.expect_op('=')
        start = self.expr()
        if not self.accept_kw('TO'):
            self.err("FOR without TO")
        limit = self.expr()
        step = None
        if self.accept_kw('STEP'):
            step = self.expr()

        ct = CTYPE[s.ty]
        conv = self.as_int if s.ty == TY_I else self.as_flt
        lim = self.newtmp('lim')
        self.emit('{')
        self.indent += 1
        self.emit('%s %s = %s;' % (ct, lim, conv(limit)))
        if step is None:
            cmp_txt = '%s <= %s' % (s.acc, lim)
            inc = '%s += 1' % s.acc
        elif self.is_literal_number(step):
            neg = step[0].lstrip('(').startswith('-')
            cmp_txt = '%s %s %s' % (s.acc, '>=' if neg else '<=', lim)
            inc = '%s += %s' % (s.acc, conv(step))
        else:
            stp = self.newtmp('stp')
            self.emit('%s %s = %s;' % (ct, stp, conv(step)))
            cmp_txt = ('(%s >= 0 ? %s <= %s : %s >= %s)'
                       % (stp, s.acc, lim, s.acc, lim))
            inc = '%s += %s' % (s.acc, stp)
        # the comparison is against plain variables: no temps, and so no
        # per-iteration release point
        self.emit('for (%s = %s; %s; %s) {'
                  % (s.acc, conv(start), cmp_txt, inc))
        self.indent += 1
        self.blocks.append(['for', canon, self.lineno])

    def is_literal_number(self, v):
        code = v[0]
        for ch in code:
            if not (is_digit(ch) or ch in '-+.()LlEe'):
                return False
        return True

    def do_next(self):
        names = []
        while not self.stmt_end():
            t = self.nxt()
            if t[0] != T_ID:
                self.err("bad NEXT")
            names.append(split_suffix(t[1])[0])
            if not self.accept_op(','):
                break
        if not names:
            names = [None]
        for nm in names:
            if not self.blocks or self.blocks[-1][0] != 'for':
                self.err("NEXT without FOR")
            blk = self.blocks[-1]
            if nm is not None and blk[1] != nm:
                self.err("NEXT %s does not match FOR %s" % (nm, blk[1]))
            self.blocks.pop()
            self.indent -= 1
            self.emit('}')
            self.indent -= 1
            self.emit('}')

    # -- DO / LOOP / WHILE ------------------------------------------------
    def do_do(self):
        if self.accept_kw('WHILE'):
            c, used = self.cond_release()
            self.emit('while (%s) {'
                      % (self.loop_cond(c) if used else c))
            self.indent += 1
            self.blocks.append(['do', 'head', self.lineno])
            return
        if self.accept_kw('UNTIL'):
            c, used = self.cond_release()
            c = '!(%s)' % c
            self.emit('while (%s) {'
                      % (self.loop_cond(c) if used else c))
            self.indent += 1
            self.blocks.append(['do', 'head', self.lineno])
            return
        self.emit('do {')
        self.indent += 1
        self.blocks.append(['do', 'tail', self.lineno])

    def do_loop(self):
        if not self.blocks or self.blocks[-1][0] != 'do':
            self.err("LOOP without DO")
        blk = self.blocks.pop()
        self.indent -= 1
        if blk[1] == 'head':
            if not self.stmt_end():
                self.err("this DO already has its test at the top")
            self.emit('}')
            return
        if self.accept_kw('UNTIL'):
            c, used = self.cond_release()
            c = '!(%s)' % c
            self.emit('} while (%s);'
                      % (self.loop_cond(c) if used else c))
        elif self.accept_kw('WHILE'):
            c, used = self.cond_release()
            self.emit('} while (%s);'
                      % (self.loop_cond(c) if used else c))
        else:
            self.emit('} while (1);')

    def do_while(self):
        c, used = self.cond_release()
        self.emit('while (%s) {' % (self.loop_cond(c) if used else c))
        self.indent += 1
        self.blocks.append(['while', self.lineno])

    # -- SELECT CASE -------------------------------------------------------
    def do_select(self):
        if not self.accept_kw('CASE'):
            self.err("SELECT without CASE")
        v = self.expr()
        name = self.newtmp('sel')
        self.emit('{')
        self.indent += 1
        if v[1] == TY_S:
            self.emit('char %s[MM_STRSZ]; mm_sset(%s, %s);'
                      % (name, name, v[0]))
            self.tmp_used = True
        else:
            self.emit('%s %s = %s;' % (CTYPE[v[1]], name, v[0]))
        self.emit('if (0) {')
        self.indent += 1
        self.blocks.append(['select', name, v[1], self.lineno])

    def do_case(self):
        if not self.blocks or self.blocks[-1][0] != 'select':
            self.err("CASE outside SELECT CASE")
        blk = self.blocks[-1]
        name, ty = blk[1], blk[2]
        self.indent -= 1
        if self.accept_kw('ELSE'):
            self.emit('} else {')
            self.indent += 1
            return
        tests = []
        while True:
            tests.append(self.case_test(name, ty))
            if not self.accept_op(','):
                break
        self.emit('} else if (%s) {' % ' || '.join(tests))
        self.indent += 1

    def case_test(self, name, ty):
        def cmpv(op, code, cty):
            if ty == TY_S:
                if cty != TY_S:
                    self.err("CASE type mismatch")
                return '(mm_scmp(%s, %s) %s 0)' % (name, code, op)
            return '((%s) %s (%s))' % (name, op, code)

        if self.accept_kw('IS'):
            t = self.nxt()
            if t[0] != T_OP or t[1] not in ('=', '<>', '<', '>', '<=', '>='):
                self.err("CASE IS needs a comparison operator")
            op = '==' if t[1] == '=' else ('!=' if t[1] == '<>' else t[1])
            v = self.expr()
            return cmpv(op, v[0], v[1])
        t = self.peek()
        if t is not None and t[0] == T_OP and t[1] in ('=', '<>', '<', '>',
                                                       '<=', '>='):
            self.i += 1
            op = '==' if t[1] == '=' else ('!=' if t[1] == '<>' else t[1])
            v = self.expr()
            return cmpv(op, v[0], v[1])
        lo = self.expr()
        if self.accept_kw('TO'):
            hi = self.expr()
            return '(%s && %s)' % (cmpv('>=', lo[0], lo[1]),
                                   cmpv('<=', hi[0], hi[1]))
        return cmpv('==', lo[0], lo[1])

    # -- EXIT / GOTO / END --------------------------------------------------
    def do_exit(self):
        if self.accept_kw('SUB'):
            self.emit(self.routine_exit() + ' return;')
            return
        if self.accept_kw('FUNCTION'):
            self.emit(self.routine_exit() + ' return __ret;')
            return
        if self.accept_kw('FOR') or self.accept_kw('DO'):
            self.emit('break;')
            return
        if self.stmt_end():
            # bare EXIT: the manual documents it as "exit a DO loop", but
            # real programs also use it inside a SUB to mean EXIT SUB, so
            # take whichever the enclosing block actually is
            for blk in reversed(self.blocks):
                if blk[0] in ('for', 'do', 'while'):
                    self.emit('break;')
                    return
                if blk[0] == 'routine':
                    break
            if self.cur is not None:
                self.warn("bare EXIT inside %s with no enclosing loop; "
                          "treated as EXIT %s" % (self.cur.name,
                          'FUNCTION' if self.cur.is_func else 'SUB'))
                if self.cur.is_func:
                    self.emit(self.routine_exit() + ' return __ret;')
                else:
                    self.emit(self.routine_exit() + ' return;')
                return
            self.err("bare EXIT is outside any loop, SUB or FUNCTION")
        self.err("unknown EXIT variant")

    def int_target(self):
        """An interrupt target that may be a literal 0 meaning "off"."""
        t = self.peek()
        if t is not None and t[0] == T_NUM and t[1].strip('()') == '0':
            self.nxt()
            return '0'
        return self.int_handler()

    def do_on_key(self):
        """ON KEY handler          fires while a key is waiting
           ON KEY 0                off
           ON KEY code, handler    fires on that key, which is consumed
           ON KEY code, 0          off

        The two forms differ in what happens to the key, and that is the
        point of having both: the any-key form leaves it for INKEY$ in
        the handler, the specific form eats it (PicoMite.c:932-935).
        MMBasic tells them apart by the argument count; here the first
        item does it - a name is a handler, a number is a key code."""
        self.uses_interrupts = True
        t = self.peek()
        if t is not None and t[0] == T_ID:
            self.emit('mmi_onkey_any(%s);' % self.int_handler())
            return
        code = self.as_int(self.expr())
        if not self.accept_op(','):
            # "ON KEY 0" with nothing after it is the any-key form off.
            self.emit('mmi_onkey_any(0);')
            return
        self.emit('mmi_onkey_sel(%s, %s);' % (code, self.int_target()))

    def do_i2c0(self):
        """I2C WRITE addr, option, count, d1 [, d2 ...]
           I2C READ  addr, option, count, <destination>
           I2C CHECK addr

        The FIXED bus: GP20/GP21, the QWIIC socket and the DS3231
        together.  No SETPIN, no OPEN, no CLOSE - the pins are the
        board's and the controller is already running for the clock,
        which is why MMBasic's cmd_i2c has no pin test where cmd_i2c2
        errors "Pin not set for I2C2".

        NOTHING HERE RAISES: MMBasic records the outcome in MM.I2C and
        returns.  See mmb_i2c.h.
        """
        self.i += 1
        self.uses_i2c0 = True
        if self.accept_kw('CHECK'):
            addr = self.as_int(self.expr())
            self.emit('mmi2c0_check(%s);' % addr)
            return
        if self.is_kw('OPEN') or self.is_kw('CLOSE'):
            self.err("the fixed I2C bus is always open - GP20/GP21 are "
                     "the board's and the controller runs for the clock; "
                     "OPEN and CLOSE are I2C2's")
        wr = self.accept_kw('WRITE')
        if not wr and not self.accept_kw('READ'):
            self.err("I2C takes WRITE, READ or CHECK")
        addr = self.as_int(self.expr())
        self.expect_op(',')
        opt = self.as_int(self.expr())
        self.expect_op(',')
        n = self.as_int(self.expr())
        self.expect_op(',')
        self.tmp_used = True
        if wr:
            def call(src, isbytes):
                self.emit('  mmi2c0_write%s(%s, %s, %s, %s);'
                          % ('_bytes' if isbytes else '', addr, opt, n, src))
            self.comms_tx('I2C WRITE', n, call)
        else:
            def call(dst, isbytes):
                self.emit('  mmi2c0_read%s(%s, %s, %s, %s);'
                          % ('_bytes' if isbytes else '',
                             addr, opt, n, dst))
            self.comms_rx('I2C READ', n, call)

    def do_i2c2(self):
        """I2C2 OPEN speed, timeout
           I2C2 WRITE addr, option, count, d1 [, d2 ...]
           I2C2 READ  addr, option, count, array()
           I2C2 CLOSE

        The second controller, on whatever header pins SETPIN gave it.
        MMBasic's split: the fixed bus needs no OPEN because it has
        fixed pins, and this one does because it has none."""
        self.i += 1
        self.uses_i2c = True
        if self.accept_kw('CLOSE'):
            self.emit('mmi2c_close();')
            return
        if self.accept_kw('OPEN'):
            speed = self.as_int(self.expr())
            self.expect_op(',')
            tmo = self.as_int(self.expr())
            self.emit('mmi2c_open(__mmi2c_sda, __mmi2c_scl, %s, %s);'
                      % (speed, tmo))
            return
        wr = self.accept_kw('WRITE')
        if not wr and not self.accept_kw('READ'):
            self.err("I2C2 takes OPEN, WRITE, READ or CLOSE")
        addr = self.as_int(self.expr())
        self.expect_op(',')
        opt = self.as_int(self.expr())
        self.expect_op(',')
        n = self.as_int(self.expr())
        self.expect_op(',')
        self.tmp_used = True
        # All three of MMBasic's forms for the data, because its own
        # BMP180 example uses two of them in the same program: a list of
        # byte expressions, a whole numeric array written a(), and a
        # STRING - and the string is the interesting one, since
        # STR2BIN() then pulls the sensor's 16- and 32-bit fields
        # straight out of what was read.  A read that only knew about
        # arrays could not run that program at all.
        if wr:
            def call(src, isbytes):
                self.emit('  mmi2c_write%s(%s, %s, %s, %s);'
                          % ('_bytes' if isbytes else '', addr, opt, n, src))
            self.comms_tx('I2C2 WRITE', n, call)
        else:
            def call(dst, isbytes):
                self.emit('  mmi2c_read%s(%s, %s, %s, %s);'
                          % ('_bytes' if isbytes else '',
                             addr, opt, n, dst))
            self.comms_rx('I2C2 READ', n, call)

    def do_web(self):
        """The WEB family, arriving in stages (PLAN-web.md §11).
        Stage 1, UDP:

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
        statement - a compiled program owns its own sockets
        (PLAN-web.md §3.2); the OPTION spelling is accepted as an
        alias so WebMite listings move across unedited.  Anything else
        under WEB names the stage it is waiting on rather than
        pretending to be an unknown command."""
        if self.accept_kw('UDP'):
            if self.is_kw('SERVER') and self.is_kw('PORT', 1):
                self.i += 2
                self.uses_udp = True
                self.emit('mmg_udp_port(%s);' % self.as_int(self.expr()))
                return
            if self.accept_kw('INTERRUPT'):
                self.uses_udp = True
                self.uses_interrupts = True
                self.emit('mmi_udp_int(%s);' % self.int_target())
                return
            if self.accept_kw('SEND'):
                self.uses_udp = True
                ip = self.as_str(self.expr())
                self.expect_op(',')
                port = self.as_int(self.expr())
                self.expect_op(',')
                msg = self.as_str(self.expr())
                self.emit('mmg_udp_send(%s, %s, %s);' % (ip, port, msg))
                return
            self.err("WEB UDP takes SERVER PORT, INTERRUPT or SEND")
        if self.accept_kw('OPEN'):
            # TCP and TLS CLIENT differ by one flag: the protocol on
            # the socket, and the SNI name the header sends first -
            # the two lines tlsget.c promised.
            tls = -1
            if self.is_kw('TCP') and self.is_kw('CLIENT', 1):
                tls = 0
            elif self.is_kw('TLS') and self.is_kw('CLIENT', 1):
                tls = 1
            if tls >= 0:
                self.i += 2
                self.uses_webclient = True
                host = self.as_str(self.expr())
                self.expect_op(',')
                port = self.as_int(self.expr())
                tmo = '5000'
                if self.accept_op(','):
                    tmo = self.as_int(self.expr())
                self.emit('mmg_webc_open(%s, %s, %s, %d);'
                          % (host, port, tmo, tls))
                return
            self.err("this WEB OPEN form is not implemented yet - the "
                     "family arrives in stages (PLAN-web.md)")
        if self.is_kw('TLS'):
            if self.is_kw('CA', 1):
                self.i += 2
                self.uses_webclient = True
                self.emit('mmg_webc_tlsca(%s);'
                          % self.as_str(self.expr()))
                return
            if self.is_kw('NOVERIFY', 1):
                self.i += 2
                self.uses_webclient = True
                self.emit('mmg_webc_tlsnoverify();')
                return
            self.err("WEB TLS takes CA or NOVERIFY")
        if self.accept_kw('CLOSE'):
            if self.is_kw('TCP') and self.is_kw('CLIENT', 1):
                self.i += 2
                self.uses_webclient = True
                self.emit('mmg_webc_close();')
                return
            self.err("this WEB CLOSE form is not implemented yet - the "
                     "family arrives in stages (PLAN-web.md)")
        if self.is_kw('TCP') and self.is_kw('CLIENT', 1):
            self.i += 2
            self.uses_webclient = True
            if self.accept_kw('REQUEST'):
                req = self.as_str(self.expr())
                self.expect_op(',')
                ptr, cells = self.lsref()
                tmo = '5000'
                if self.accept_op(','):
                    tmo = self.as_int(self.expr())
                self.emit('mmg_webc_request(%s, %s, %s, %s);'
                          % (req, ptr, cells, tmo))
                return
            if self.accept_kw('READ'):
                ptr, cells = self.lsref()
                tmo = '5000'
                if self.accept_op(','):
                    tmo = self.as_int(self.expr())
                self.emit('mmg_webc_read(%s, %s, %s);'
                          % (ptr, cells, tmo))
                return
            if self.accept_kw('WRITE'):
                ptr, cells = self.lsref()
                tmo = '10000'
                if self.accept_op(','):
                    tmo = self.as_int(self.expr())
                self.emit('mmg_webc_write(%s, %s);' % (ptr, tmo))
                return
            self.err("WEB TCP CLIENT takes REQUEST, READ or WRITE "
                     "(STREAM arrives in stages - PLAN-web.md)")
        # the server family - stage 4
        if self.is_kw('TCP') and self.is_kw('SERVER', 1) \
                and self.is_kw('PORT', 2):
            self.i += 3
            self.uses_webserver = True
            self.emit('mmg_webs_port(%s);' % self.as_int(self.expr()))
            return
        if self.is_kw('TCP') and self.is_kw('INTERRUPT', 1):
            self.i += 2
            self.uses_webserver = True
            self.uses_interrupts = True
            self.emit('mmi_webs_int(%s);' % self.int_target())
            return
        if self.is_kw('TCP') and self.is_kw('READ', 1):
            self.i += 2
            self.uses_webserver = True
            conn = self.as_int(self.expr())
            self.expect_op(',')
            ptr, cells = self.lsref()
            self.emit('mmg_webs_read(%s, %s, %s);' % (conn, ptr, cells))
            return
        if self.is_kw('TCP') and self.is_kw('SEND', 1):
            self.i += 2
            self.uses_webserver = True
            conn = self.as_int(self.expr())
            self.expect_op(',')
            ptr, cells = self.lsref()
            self.emit('mmg_webs_send(%s, %s);' % (conn, ptr))
            return
        if self.is_kw('TCP') and self.is_kw('CLOSE', 1):
            self.i += 2
            self.uses_webserver = True
            self.emit('mmg_webs_close(%s);' % self.as_int(self.expr()))
            return
        if self.accept_kw('TRANSMIT'):
            if self.accept_kw('CODE'):
                conn = self.as_int(self.expr())
                self.expect_op(',')
                self.uses_webserver = True
                self.emit('mmg_webs_code(%s, %s);'
                          % (conn, self.as_int(self.expr())))
                return
            if self.accept_kw('FILE'):
                conn = self.as_int(self.expr())
                self.expect_op(',')
                fname = self.as_str(self.expr())
                self.expect_op(',')
                mime = self.as_str(self.expr())
                self.uses_webserver = True
                self.emit('mmg_webs_file(%s, %s, %s);'
                          % (conn, fname, mime))
                return
            if self.accept_kw('PAGE'):
                self.do_web_page()
                return
            self.err("WEB TRANSMIT takes CODE, FILE or PAGE")
        if self.accept_kw('NTP'):
            # WEB NTP [offset [, server$ [, timeout]]] - cmd_ntp
            # (MMntp.c) mapped onto ntpdate(8); mmb_net.h says how.
            # The timeout is parsed and dropped: ntpdate carries its
            # own retry cadence, inside MMBasic's 5 s default.
            self.uses_net = True
            off = '0.0'
            server = c_string_literal('')
            if not self.stmt_end():
                off = self.as_flt(self.expr())
                if self.accept_op(','):
                    server = self.as_str(self.expr())
                    if self.accept_op(','):
                        self.as_int(self.expr())
            self.emit('mmg_web_ntp(%s, %s);' % (off, server))
            return
        if self.accept_kw('PING'):
            # WEB PING addr$ [, count] - ping(8) with its output on
            # the console; the replicated WebMite build has no PING
            # of its own, so the mapping is the reference
            # (PLAN-web.md 12.2).
            self.uses_net = True
            addr = self.as_str(self.expr())
            cnt = '4'
            if self.accept_op(','):
                cnt = self.as_int(self.expr())
            self.emit('mmg_web_ping(%s, %s);' % (addr, cnt))
            return
        if self.accept_kw('CONNECT'):
            # No arguments: the WebMite's link gate - error "WIFI not
            # connected" when the radio has no address.  With ssid$,
            # pass$: wifi(8) joins and waits, NOT persisted -
            # /etc/wifi.conf stays the owner of the boot-time join
            # (PLAN-web.md 12.2).
            self.uses_net = True
            if self.stmt_end():
                self.emit('mmg_web_connect_chk();')
                return
            ssid = self.as_str(self.expr())
            self.expect_op(',')
            key = self.as_str(self.expr())
            self.emit('mm_run_begin();')
            self.emit('mm_run_arg(%s);' % c_string_literal('wifi'))
            self.emit('mm_run_arg(%s);' % ssid)
            self.emit('mm_run_arg(%s);' % key)
            self.emit('mm_run_exec();')
            return
        self.err("this WEB command is not implemented yet - the family "
                 "arrives in stages (PLAN-web.md)")

    def do_web_page(self):
        """WEB TRANSMIT PAGE conn, "file" [, bufsize] - PLAN-web.md §4,
        the call-site substitution.  The page is read HERE, at
        translate time, and every {expression} in it is compiled
        through the normal expression pipeline INLINE in this
        statement - where the enclosing sub's locals and parameters
        are simply in scope, which is what lets retic.bas write
        {Title(pnbr)} and {str$(pnbr+1)}.  The emitted switch is
        dispatched by mm_webpg_next matching each brace's normalised
        text against the __mmwebsub_N table, so a page reorganised on
        the card keeps working; an expression the table has never
        seen raises at run time, naming it."""
        conn = self.as_int(self.expr())
        self.expect_op(',')
        t = self.peek()
        if t is None or t[0] != T_STR:
            self.err("WEB TRANSMIT PAGE needs a literal page name: a "
                     "computed one cannot be pre-scanned (PLAN-web.md)")
        self.i += 1
        fname = t[1]
        bufsize = '4096'
        if self.accept_op(','):
            bufsize = self.as_int(self.expr())
        self.uses_webserver = True
        if self.mode != 'emit':
            return
        # next to the program, or absolute - and it must exist NOW
        d = self.srcname.replace('\\', '/').rsplit('/', 1)
        base = d[0] + '/' if len(d) == 2 else ''
        path = fname if fname[:1] == '/' else base + fname
        try:
            f = open(path, 'rb')
            text = f.read().decode('latin-1')
            f.close()
        except OSError:
            self.err("cannot read page '%s': it must exist at "
                     "translate time, next to the program" % fname)
        keys, raws = websub_scan(text)
        tno = len(self.websubs)
        self.websubs.append(keys)
        self.emit('{ struct mm_webpg __pg; int __pi;')
        self.emit('mm_webpg_start(&__pg, %s, %s, %s);'
                  % (conn, c_string_literal(fname), bufsize))
        self.emit('while ((__pi = mm_webpg_next(&__pg, __mmwebsub_%d, '
                  '%d)) >= 0) {' % (tno, len(keys)))
        self.emit('    unsigned __pgm = mm_mark();')
        self.emit('    switch (__pi) {')
        save = (self.toks, self.i)
        for ci, raw in enumerate(raws):
            self.toks = tokenize(raw, self.lineno)
            self.i = 0
            v = self.expr()
            if not self.at_end():
                self.err("page expression '{%s}' does not parse as one "
                         "expression" % raw)
            if v[1] == TY_S:
                put = 'mm_webpg_put_s'
            elif v[1] == TY_F:
                put = 'mm_webpg_put_f'
            else:
                put = 'mm_webpg_put_i'
            self.emit('    case %d: %s(&__pg, %s); break;'
                      % (ci, put, v[0]))
        self.toks, self.i = save
        self.emit('    }')
        self.emit('    mm_release(__pgm);')
        self.emit('}')
        self.emit('mm_webpg_send(&__pg); }')

    def do_onewire(self):
        """ONEWIRE RESET pin
           ONEWIRE WRITE pin, flag, count, <data>
           ONEWIRE READ  pin, flag, count, <destination>

        The data and destination are the shared forms - MMBasic's
        owWrite and owRead call GetCommsTxData and GetCommsRxDest at
        argument 6, exactly as I2C does.  That is why one-wire waited
        for mmb_comms.h rather than growing a third copy of them."""
        self.uses_gpio = True
        self.uses_onewire = True
        if self.accept_kw('RESET'):
            pin = self.as_int(self.expr())
            self.emit('(void)mmow_reset(%s);' % pin)
            return
        wr = self.accept_kw('WRITE')
        if not wr and not self.accept_kw('READ'):
            self.err("ONEWIRE takes RESET, WRITE or READ")
        pin = self.as_int(self.expr())
        self.expect_op(',')
        flag = self.as_int(self.expr())
        self.expect_op(',')
        n = self.as_int(self.expr())
        self.expect_op(',')
        self.tmp_used = True
        if wr:
            def call(src, isbytes):
                self.emit('  mmow_write%s(%s, %s, %s, %s);'
                          % ('_bytes' if isbytes else '', pin, flag, n, src))
            self.comms_tx('ONEWIRE WRITE', n, call)
        else:
            def call(dst, isbytes):
                self.emit('  mmow_read%s(%s, %s, %s, %s);'
                          % ('_bytes' if isbytes else '', pin, flag, n, dst))
            self.comms_rx('ONEWIRE READ', n, call)

    # -- the data arguments I2C, SPI and one-wire share -----------------
    #
    # MMBasic has ONE implementation of these and three callers:
    # GetCommsTxData, GetCommsRxDest and PutCommsRxData in MMBasic.c,
    # reached from I2C.c, Onewire.c and (through GetSendDataList and
    # GetReceiveDataBuffer) SPI.c.  The buses take the same forms and a
    # program expects them to, so this is one implementation too.
    #
    # Everything lands in the shared value buffer of mmb_comms.h, except
    # a string source, whose bytes are already bytes and go straight to
    # the bus.  See that header for why the buffer holds values rather
    # than bytes - an SPI word can be 16 bits.

    def comms_tx(self, what, n, emit_call):
        """Parse a TX data argument and emit the transfer.

        `emit_call(src, isbytes)` writes the statement, given either the
        buffer or a byte pointer.  `what` names the statement in errors.
        """
        self.uses_comms = True
        if self.accept_kw('LONGSTRING'):
            # SPI WRITE n, LONGSTRING a() - the bytes of a long string,
            # with no 255-byte cap and no copy.  Spelled out because a
            # long string IS an integer array: written a() it is a
            # numeric array and sends one byte per eight-byte cell,
            # which is MMBasic's behaviour and stays.
            ptr, _cells = self.lsref()
            self.emit('{ const unsigned char *__b = mmc_tx_ls(%s, %s);'
                      % (ptr, n))
            emit_call('__b', True)
            self.emit('}')
            return
        if self.is_array_arg():
            s = self.arrayref()
            if s.ty == TY_S:
                self.err("%s needs a numeric array, and '%s' is a string "
                         "array" % (what, s.name))
            base, cnt = self.array_flat(s)
            self.emit('{ unsigned int *__b = mmc_buf_for(%s);' % n)
            self.emit('  mmc_tx_arr_%s(__b, %s, %s, %s);'
                      % ('i' if s.ty == TY_I else 'f', n, base, cnt))
            emit_call('__b', False)
            self.emit('}')
            return
        v0 = self.expr()
        if v0[1] == TY_S and not self.is_op(','):
            # A string: no copy, and no buffer.  MMBasic copies because
            # its buffer is the only path it has; mmc_tx_str only checks
            # the length and hands back where the bytes already are.
            self.emit('{ const unsigned char *__b = mmc_tx_str(%s, %s);'
                      % (v0[0], n))
            emit_call('__b', True)
            self.emit('}')
            return
        # A list of expressions.  MMBasic requires as many as the count
        # says and raises "Argument count" otherwise - which the old
        # per-bus code did not check, so a short list left the driver
        # reading past the buffer.
        vals = [self.as_int(v0)]
        while self.accept_op(','):
            vals.append(self.as_int(self.expr()))
        self.emit('{ unsigned int *__b;')
        self.emit('  mmc_count(%s, %d);' % (n, len(vals)))
        self.emit('  __b = mmc_buf_for(%s);' % n)
        for i, v in enumerate(vals):
            self.emit('  __b[%d] = (unsigned int)(%s);' % (i, v))
        emit_call('__b', False)
        self.emit('}')

    def comms_rx(self, what, n, emit_call):
        """Parse an RX destination and emit the transfer.

        `emit_call(buf)` writes the statement that fills the buffer.
        """
        self.uses_comms = True
        if self.accept_kw('LONGSTRING'):
            # ... and the same as a destination, which is how a program
            # reads more than 255 bytes back.
            ptr, cells = self.lsref()
            self.emit('{ unsigned char *__b = mmc_rx_ls(%s, %s, %s);'
                      % (ptr, cells, n))
            emit_call('__b', True)
            self.emit('}')
            return
        if self.is_array_arg():
            s = self.arrayref()
            if s.ty == TY_S:
                self.err("%s needs a numeric array, and '%s' is a string "
                         "array" % (what, s.name))
            base, cnt = self.array_flat(s)
            # Checked BEFORE the transfer, as GetCommsRxDest is: a read
            # moves the bus, so a destination that cannot hold the
            # answer has to be refused before it does.
            self.emit('{ unsigned int *__b = mmc_buf_for(%s);' % n)
            self.emit('  mmc_rx_fits(%s, %s);' % (cnt, n))
            emit_call('__b', False)
            self.emit('  mmc_rx_arr_%s(%s, %s, __b, %s);'
                      % ('i' if s.ty == TY_I else 'f', base, cnt, n))
            self.emit('}')
            return
        t = self.peek()
        if t is not None and t[0] == T_ID and not self.is_op(',', 1):
            s = self.reference(t[1], self.is_op('(', 1))
            if s.ty == TY_S and not s.is_array:
                self.i += 1
                self.emit('{ unsigned int *__b = mmc_buf_for(%s);' % n)
                self.emit('  mmc_rx_strfits(%s);' % n)
                emit_call('__b', False)
                self.emit('  mmc_rx_str(%s, __b, %s);' % (s.acc, n))
                self.emit('}')
                return
        # A list of lvalues, one per value received - MMBasic's
        # COMMS_RXD_LIST, which was missing here entirely.  A single
        # scalar is the same form with one element, which is also
        # MMBasic's rule that the count must then be 1.
        tgts = [self.lvalue_from_here()]
        while self.accept_op(','):
            tgts.append(self.lvalue_from_here())
        self.emit('{ unsigned int *__b;')
        self.emit('  mmc_count(%s, %d);' % (n, len(tgts)))
        self.emit('  __b = mmc_buf_for(%s);' % n)
        emit_call('__b', False)
        for i, t in enumerate(tgts):
            self.emit('  %s = __b[%d];' % (t, i))
        self.emit('}')

    def do_spi(self):
        """SPI OPEN speed, mode [, bits]
           SPI WRITE n, d1 [, d2 ...] | n, array() | n, string$
           SPI READ  n, var
           SPI CLOSE

        The FIRST controller: SPI2 is the second one, which on this
        board is the SD card's, so it is not offered.  Chip select is
        the program's, as it is on a PicoMite."""
        self.i += 1
        self.uses_spi = True
        if self.accept_kw('CLOSE'):
            self.emit('mmspi_close();')
            return
        if self.accept_kw('OPEN'):
            speed = self.as_int(self.expr())
            self.expect_op(',')
            mode = self.as_int(self.expr())
            # bits is optional and 8 unless given, as in MMBasic
            bits = '8'
            if self.accept_op(','):
                bits = self.as_int(self.expr())
            self.emit('mmspi_open(__mmspi_a, __mmspi_b, __mmspi_c, '
                      '%s, %s, %s);' % (speed, mode, bits))
            return
        wr = self.accept_kw('WRITE')
        if not wr and not self.accept_kw('READ'):
            self.err("SPI takes OPEN, WRITE, READ or CLOSE")
        n = self.as_int(self.expr())
        self.expect_op(',')
        self.tmp_used = True
        # The same three data forms I2C2 takes, and for the same reason:
        # a display wants a whole run in one call, and MMBasic's own
        # GetSendDataList accepts a list, an array or a string.
        if wr:
            def call(src, isbytes):
                self.emit('  mmspi_write%s(%s, %s);'
                          % ('_bytes' if isbytes else '', n, src))
            self.comms_tx('SPI WRITE', n, call)
        else:
            def call(dst, isbytes):
                self.emit('  mmspi_read%s(%s, %s);'
                          % ('_bytes' if isbytes else '', n, dst))
            self.comms_rx('SPI READ', n, call)

    def settick_id(self):
        """SETTICK's optional trailing timer number, 1-4.  Absent is 1,
        which is MMBasic's irq = 0 when the argument is missing."""
        if self.accept_op(','):
            return self.as_int(self.expr())
        return '1'

    def do_settick(self):
        """SETTICK period, handler [, n]
           SETTICK 0, 0 [, n]          off
           SETTICK PAUSE|RESUME [, n]

        MMBasic counts milliseconds in an interrupt and fires when the
        count passes the period; this keeps a microsecond deadline and
        asks at the poll that is already happening.  The observable
        rules are copied: four timers, missed periods dropped rather
        than queued, and PAUSE freezing the time-to-go where it stands.
        """
        self.i += 1
        self.uses_interrupts = True
        if self.accept_kw('PAUSE'):
            self.emit('mmi_settick_pause(%s, 0);' % self.settick_id())
            return
        if self.accept_kw('RESUME'):
            self.emit('mmi_settick_pause(%s, 1);' % self.settick_id())
            return
        ms = self.as_int(self.expr())
        self.expect_op(',')
        # "SETTICK 0, 0" turns a timer off, and its handler slot is a
        # literal 0 rather than a name - so the target is only resolved
        # when there is one to resolve.
        fn = self.int_target()
        self.emit('mmi_settick(%s, %s, %s);' % (ms, fn, self.settick_id()))
        return

    def setpin_pull(self):
        """MMBasic's optional PULLUP / PULLDOWN on an input SETPIN.

        Absent means neither, which is MMBasic's default (External.c:
        1918-1935 leaves option = 0).  Hysteresis is not an option in
        either place - every digital input gets the Schmitt trigger."""
        if not self.accept_op(','):
            return '0'
        if self.accept_kw('PULLUP'):
            return '1'
        if self.accept_kw('PULLDOWN'):
            return '-1'
        self.err("SETPIN's last argument is PULLUP or PULLDOWN")

    def int_handler(self):
        """Resolve an interrupt target to the C function that is it.

        MMBasic's GetIntAddress (MM_Misc.c:10250) takes a SUB name, a
        label or a line number.  Only the SUB survives translation:
        compiled code cannot jump into the middle of a function from a
        poll site, so labels and line numbers are refused here with a
        clear message rather than half-working - the ON ERROR RESTART
        precedent.  The SUB is otherwise an ordinary generated function
        and may still be called normally."""
        t = self.nxt()
        if t[0] != T_ID:
            self.err("an interrupt handler must be the name of a SUB "
                     "(MMBasic's labels and line numbers are not "
                     "translated)")
        canon, _ = split_suffix(t[1])
        r = self.routines.get(canon)
        if r is None:
            self.err("no SUB called '%s' to handle the interrupt" % t[1])
        if r.is_func:
            self.err("'%s' is a FUNCTION; an interrupt handler must be "
                     "a SUB" % t[1])
        if r.params:
            self.err("interrupt handler '%s' must take no parameters"
                     % t[1])
        return r.cname

    def note_goto(self, canon):
        depth = 0
        for blk in self.blocks:
            if blk[0] != 'routine':
                depth += 1
        old = self.goto_depth.get(canon)
        if old is None or depth < old:
            self.goto_depth[canon] = depth

    def do_goto(self):
        t = self.nxt()
        if t[0] == T_NUM and t[2] == 'I':
            canon = t[1]
        elif t[0] != T_ID:
            self.err("GOTO needs a label or line number")
        else:
            canon = split_suffix(t[1])[0]
        if canon not in self.labels:
            self.err("unknown label '%s'" % t[1])
        self.labels_used[canon] = 1
        self.note_goto(canon)
        self.emit('goto %s;' % clabel(canon))

    def do_end(self):
        # Inside a single-line IF, END SUB means RETURN NOW, not "the
        # routine's text stops here" - `If done Then End Sub` is the
        # ordinary MMBasic way to leave a SUB early, and closing the
        # block there would end the routine in the middle of itself.
        if self.inline:
            if self.accept_kw('SUB'):
                self.emit(self.routine_exit() + ' return;')
                return
            if self.accept_kw('FUNCTION'):
                self.emit(self.routine_exit() + ' return __ret;')
                return
        if self.accept_kw('SUB'):
            self.close_routine(False)
            return
        if self.accept_kw('FUNCTION'):
            self.close_routine(True)
            return
        if self.accept_kw('IF'):
            self.close_block('if')
            return
        if self.accept_kw('SELECT'):
            self.close_block('select')
            self.indent -= 1
            self.emit('}')
            return
        self.skip_statement()
        self.emit('mm_end();')

    # -- SUB / FUNCTION bodies ----------------------------------------------
    def open_routine(self, is_func):
        t = self.nxt()
        canon = split_suffix(t[1])[0]
        r = self.routines.get(canon)
        if r is None:
            self.err("internal: routine '%s' not found" % canon)
        self.skip_statement()
        self.cur = r
        self.out = self.out_body
        self.indent = 0
        self.raw('')
        self.raw(self.signature(r) + ' {')
        self.indent = 1
        self.emit('unsigned __mark = mm_mark(); (void)__mark;')
        if r.heap_locals:
            self.emit('struct mm_l_%s *__L = mm_lheap(sizeof *__L);'
                      % r.cname)
        # hoist every local declaration to the top of the C function,
        # in declaration order so the output does not depend on the host
        # Python's dictionary ordering
        for nm in r.local_order:
            s = r.locals[nm]
            if s.is_param:
                continue
            self.emit_local_decl(s)
        if r.is_func:
            if r.ty == TY_S:
                self.emit('__ret[0] = 0; __ret[1] = 0;')
            else:
                self.emit('%s __ret = 0;' % CTYPE[r.ty])
        if self.uses_onerror:
            # Entered with an error already recorded - which means an
            # argument expression raised - so do nothing and go back.  The
            # interpreter never gets here at all: it jumps away before the
            # call.  Returning at once is the same thing observably, and
            # it spends none of the skip count on statements in here.
            ret = ' return __ret;' if r.is_func else ' return;'
            # a lexical SKIP window never crosses into a routine body
            self.err_window = 0
            self.err_window_pending = None
            self.emit('if (__mm_e[0]) { %s%s }'
                      % (self.routine_exit(), ret))
            # The SUB/FUNCTION line is itself a statement the interpreter
            # executes and counts on every call, so entering costs one of
            # the skip count.  Without this our count ran one statement
            # further into a called routine than a real PicoMite's did -
            # which is exactly where the side-by-side disagreed.
            self.emit('if (__mm_e[1]) { mm_pr_commit(); __mm_e[0] = 0;'
                      ' if (__mm_e[1] > 0) __mm_e[1]--; }')
        self.blocks.append(['routine', self.lineno])

    def emit_local_decl(self, s):
        # An array, a string or a structure that is not STATIC lives in
        # the invocation's heap block, declared once in its struct and
        # zeroed by mm_lheap; there is nothing to declare here.
        if not s.is_static and (s.is_array or s.ty == TY_S
                                or s.stype is not None):
            return
        pfx = 'static ' if s.is_static else ''
        if s.is_static and s.has_init:
            self.emit('static int __once_%s = 0;'
                      % s.name.replace('.', '__'))
        if s.is_array:
            dims = ''.join('[%s]' % d for d in reversed(s.dims))
            if s.ty == TY_S:
                self.emit('%schar %s%s[%s];'
                          % (pfx, s.acc, dims, self.strsz(s)))
            else:
                self.emit('%s%s %s%s;' % (pfx, CTYPE[s.ty], s.acc, dims))
            if not s.is_static:
                self.emit('memset(%s, 0, sizeof %s);' % (s.acc, s.acc))
        elif s.ty == TY_S:
            self.emit('%schar %s[MM_STRSZ];' % (pfx, s.acc))
            if not s.is_static:
                self.emit('%s[0] = 0; %s[1] = 0;' % (s.acc, s.acc))
        else:
            if s.is_static:
                self.emit('static %s %s;' % (CTYPE[s.ty], s.acc))
            else:
                self.emit('%s %s = 0;' % (CTYPE[s.ty], s.acc))

    def calld_head(self, d):
        """The C prototype of a CALL-by-name dispatcher: the shape of
        its representative routine with the name argument added."""
        rep = d['rep']
        parts = []
        if d['is_func'] and rep.ty == TY_S:
            parts.append('char *__ret')
        parts.append('const char *__nm')
        for p in rep.params[:d['nargs']]:
            nm = 'p_' + p.name.replace('.', '__')
            if p.ty == TY_S:
                parts.append('char *%s' % nm)
            elif p.byref:
                parts.append('%s *%s' % (CTYPE[p.ty], nm))
            else:
                parts.append('%s %s' % (CTYPE[p.ty], nm))
        if not d['is_func']:
            ret = 'static void '
        elif rep.ty == TY_S:
            ret = 'static char *'
        else:
            ret = 'static ' + CTYPE[rep.ty] + ' '
        return '%s%s(%s)' % (ret, d['name'], ', '.join(parts))

    def signature(self, r):
        parts = []
        if r.is_func and r.ty == TY_S:
            parts.append('char *__ret')
        for p in r.params:
            nm = 'p_' + p.name.replace('.', '__')
            if p.stype is not None:
                parts.append('struct t_%s *%s' % (p.stype, nm))
                continue
            if p.is_array:
                if p.ty == TY_S:
                    parts.append('char (*%s)[MM_STRSZ]' % nm)
                else:
                    parts.append('%s *%s' % (CTYPE[p.ty], nm))
                # BOUND() inside the routine reads the caller's bounds
                parts.append('const MMINTEGER *__b_%s'
                             % p.name.replace('.', '__'))
            elif p.ty == TY_S:
                parts.append('char *%s' % nm)
            elif p.byref:
                parts.append('%s *%s' % (CTYPE[p.ty], nm))
            else:
                parts.append('%s %s' % (CTYPE[p.ty], nm))
        if not parts:
            parts.append('void')
        if not r.is_func:
            ret = 'void '
        elif r.ty == TY_S:
            ret = 'char *'
        else:
            ret = CTYPE[r.ty] + ' '
        return '%s%s(%s)' % (ret, r.cname, ', '.join(parts))

    def close_routine(self, is_func):
        if not self.blocks or self.blocks[-1][0] != 'routine':
            self.err("END SUB/FUNCTION without SUB/FUNCTION")
        self.blocks.pop()
        if self.cur is not None and self.cur.is_func:
            self.emit(self.routine_exit() + ' return __ret;')
        else:
            self.emit(self.routine_exit())
        self.indent = 0
        self.raw('}')
        self.cur = None
        self.out = self.out_main
        self.indent = 1

    # ==================================================================
    #  driving it all
    # ==================================================================

    def run(self):
        self.pass_fonts()
        self.pass_routine_names()
        self.pass_declarations()
        self.walk('scan')
        self.tmpn = 0
        self.out_main = []
        self.out_body = []
        self.walk('emit')
        return not self.errors

    def global_decls(self):
        """Scalars in the process image, arrays and strings on the heap.

        This is how an interpreted BASIC has always done it - a fixed
        area for simple variables, everything bulky in the heap - and on
        this machine it is also what the memory wants.  Scalars are hot:
        loop counters touched every iteration, and SRAM is 3.7x faster
        than PSRAM (44MB/s against 12, measured with psbench).  Arrays
        and strings are bulk, walked sequentially, and they are what was
        filling bcrun's 48K of VM address space - a 38,400 byte array
        does not fit in it at all.

        One struct, one allocation.  So one free at exit with nothing to
        tidy up, no fragmentation, and sizeof does the sizing - there is
        no hand-computed maximum to drift out of step with the
        declarations as they change.

        Every array bound here is a compile-time constant (mmb2c rejects
        a runtime bound by name), so sizeof covers everything that
        compiles.  When variable bounds do arrive they want a growable
        tail after the fixed members, extended by realloc - which works
        precisely because access goes through H and the block may move.
        """
        out = []
        heap = []
        names = list(self.globals.keys())
        names.sort()
        for nm in names:
            s = self.globals[nm]
            if s.is_const:
                continue
            cn = cvar(nm)
            note = ''
            if s.implied:
                note = '   /* implied, first seen line %d */' % s.where
            if s.stype is not None:
                dims = ''.join('[%s]' % d for d in reversed(s.dims)) \
                    if s.is_array else ''
                heap.append('struct t_%s %s%s;%s'
                            % (s.stype, cn, dims, note))
            elif s.is_array and s.dynamic:
                # Bounds known only at run time, so the elements cannot
                # be part of the type: a pointer and the bounds table
                # beside it, which is the shape an array parameter has.
                heap.append('%s;   /* DIM at run time */'
                            % self.dyn_decl(s, cn))
                heap.append('MMINTEGER __b_%s[MM_MAXDIM + 1];' % cn)
            elif s.is_array:
                dims = ''.join('[%s]' % d for d in reversed(s.dims))
                if s.ty == TY_S:
                    heap.append('char %s%s[%s];%s'
                                % (cn, dims, self.strsz(s), note))
                else:
                    heap.append('%s %s%s;%s'
                                % (CTYPE[s.ty], cn, dims, note))
            elif s.ty == TY_S:
                heap.append('char %s[MM_STRSZ];%s' % (cn, note))
            else:
                out.append('%s %s;%s' % (CTYPE[s.ty], cn, note))
        self.heap_used = bool(heap)
        if heap:
            out.append('')
            out.append('/* Arrays and strings: one block, allocated once'
                       ' from the PSRAM heap. */')
            out.append('struct mm_vars {')
            for h in heap:
                out.append('    ' + h)
            out.append('};')
            out.append('static struct mm_vars *H;')
        return out

    def local_structs(self):
        """One struct per routine that has LOCAL arrays or strings.

        At file scope rather than inside the function, because the FCC
        view is C89 and a type declared in a block would be a different
        type in every translation the compiler sees.  Routines come out
        in sorted order for the same reason the globals do: the C must
        not depend on the host Python's dictionary ordering.
        """
        out = []
        names = list(self.routines.keys())
        names.sort()
        for nm in names:
            r = self.routines[nm]
            if not r.heap_locals:
                continue
            out.append('')
            out.append('/* LOCAL arrays and strings of %s: one block per'
                       ' invocation. */' % r.disp)
            out.append('struct mm_l_%s {' % r.cname)
            for lnm in r.local_order:
                s = r.locals[lnm]
                if s.is_param or s.is_static:
                    continue
                if not (s.is_array or s.ty == TY_S
                        or s.stype is not None):
                    continue
                cn = cvar(lnm)
                if s.stype is not None:
                    dims = ''.join('[%s]' % d for d in reversed(s.dims)) \
                        if s.is_array else ''
                    out.append('    struct t_%s %s%s;'
                               % (s.stype, cn, dims))
                elif s.is_array and s.dynamic:
                    out.append('    %s;   /* DIM at run time */'
                               % self.dyn_decl(s, cn))
                    out.append('    MMINTEGER __b_%s[MM_MAXDIM + 1];' % cn)
                elif s.is_array:
                    dims = ''.join('[%s]' % d for d in reversed(s.dims))
                    if s.ty == TY_S:
                        out.append('    char %s%s[%s];'
                                   % (cn, dims, self.strsz(s)))
                    else:
                        out.append('    %s %s%s;'
                                   % (CTYPE[s.ty], cn, dims))
                else:
                    out.append('    char %s[MM_STRSZ];' % cn)
            out.append('};')
        return out

    def report(self):
        out = []
        out.append('Implied global variables (created by first use, never')
        out.append('declared with DIM):')
        if not self.implied:
            out.append('    (none - every variable was declared)')
        else:
            seen = {}
            for nm, ty, ln, rt in self.implied:
                if nm in seen:
                    continue
                seen[nm] = 1
                rd = self.routines[rt].disp if rt in self.routines else rt
                where = ('in ' + rd) if rt else 'in the main program'
                g = self.globals.get(nm)
                out.append('    %-20s %-8s first used line %-5d %s'
                           % (g.disp if g else nm, TYNAME[ty], ln, where))
        if self.skipped:
            out.append('')
            out.append('Lines that could not be translated.  Each is left in')
            out.append('the C as a comment; nothing was emitted for it, so')
            out.append('check the surrounding logic still makes sense:')
            for ln, text, why in self.skipped:
                out.append('    line %-5d %s' % (ln, text[:60]))
                out.append('              -> %s' % why[:64])
        # every global reached from inside a routine is worth a look
        rn = list(self.routines.keys())
        rn.sort()
        any_touch = False
        body = []
        for name in rn:
            r = self.routines[name]
            if not r.gtouch:
                continue
            any_touch = True
            body.append('    %s %s (line %d):'
                        % ('FUNCTION' if r.is_func else 'SUB     ',
                           r.disp, r.line))
            keys = list(r.gtouch.keys())
            keys.sort()
            for k in keys:
                g = self.globals.get(k)
                tag = 'implied' if (g is not None and g.implied) else 'DIMmed'
                body.append('        %-20s %-8s %-8s used line %d'
                            % (g.disp if g else k,
                               TYNAME[g.ty] if g else '?', tag,
                               r.gtouch[k]))
        if any_touch:
            out.append('')
            out.append('Globals reached from inside a SUB or FUNCTION.')
            out.append('Anything marked "implied" was never DIMmed and is')
            out.append('shared with the whole program - check that a LOCAL')
            out.append('was not what you meant:')
            out.extend(body)
        return out

    def write(self, wr):
        wr('/* Generated by mmb2c.py v%s from %s */\n' % (VERSION, self.srcname))
        wr('/*\n')
        for ln in self.report():
            wr(' * ' + ln + '\n')
        for w in self.warnings:
            wr(' * warning: ' + w + '\n')
        wr(' */\n\n')
        wr('#include "mmb_runtime.h"\n')
        # The geometry primitives are static functions in headers, so
        # they land in the program rather than in bcrun - one header
        # per primitive, one flag per header, because cc1's dead-static
        # rule counts names rather than reachability and cannot drop a
        # recursive primitive an included header carries.  The include
        # IS the granularity, so it must be exact.
        if self.uses_circle:
            wr('#include "mmb_gfx_circle.h"\n')
        if self.uses_box:
            wr('#include "mmb_gfx_box.h"\n')
        if self.uses_gui:
            wr('#include "mmb_gui.h"\n')
        if self.uses_linew:
            wr('#include "mmb_gfx_line.h"\n')
        if self.uses_rbox:
            wr('#include "mmb_gfx_rbox.h"\n')
        if self.uses_triangle:
            wr('#include "mmb_gfx_triangle.h"\n')
        if self.uses_polygon:
            wr('#include "mmb_gfx_polygon.h"\n')
        if self.uses_bezier:
            wr('#include "mmb_gfx_bezier.h"\n')
        if self.uses_fill:
            wr('#include "mmb_gfx_fill.h"\n')
        if self.uses_arc:
            wr('#include "mmb_gfx_arc.h"\n')
        if self.uses_text:
            wr('#include "mmb_gfx_text.h"\n')
        if self.uses_mappal:
            wr('#include "mmb_gfx_map.h"\n')
        # Before mmb_blit.h, and the order is load-bearing: BLIT FLASH
        # is compiled only when the slot machinery's include guard is
        # already present - the mmb_wait.h detection pattern.
        if self.uses_flash:
            wr('#include "mmb_flash.h"\n')
        if self.uses_blit:
            wr('#include "mmb_blit.h"\n')
        # After mmb_blit.h (the row workhorses) and before mmb_int.h,
        # whose collision checks exist only under this header's guard.
        if self.uses_sprite:
            wr('#include "mmb_sprite.h"\n')
        # Before mmb_int.h, whose tone-deadline check exists only under
        # this header's guard.  It defines mm_play_volume itself.
        if self.uses_playd:
            wr('#include "mmb_play.h"\n')
        if self.uses_gpio:
            wr('#include "mmb_gpio.h"\n')
        # After mmb_gpio.h, whose claims and mode table it shares:
        # WS2812 and BITSTREAM on the fixed PIO programs.
        if self.uses_pioout:
            wr('#include "mmb_pioout.h"\n')
        # After mmb_gpio.h: PORT validates against the same mmg_mode
        # table SETPIN fills in.
        if self.uses_port:
            wr('#include "mmb_port.h"\n')
        # MATH C_ADD and the rest.  Needs nothing but the runtime's own
        # types and MM_RAISE, so it can sit anywhere after that.
        if self.uses_math:
            wr('#include "mmb_math.h"\n')
        # The four CRCs, on the same terms: the engine goes in the
        # program that asks for it and costs every other program
        # nothing.
        if self.uses_crc:
            wr('#include "mmb_crc.h"\n')
        # SORT's shell sort, the same bargain as mmb_math.h: only a
        # program that sorts carries the engine.
        if self.uses_sort:
            wr('#include "mmb_sort.h"\n')
        # Whole-array ops, dynamic DIM/REDIM and the MATH() reductions:
        # the same bargain again.  mm_arr_count alone stays a runtime
        # call - every array parameter's count goes through it.
        if self.uses_array:
            wr('#include "mmb_array.h"\n')
        # LONGSTRING's memcpy arithmetic; only the two file forms keep
        # a runtime crossing (the channel is bcrun's own stream).
        if self.uses_lstring:
            wr('#include "mmb_lstring.h"\n')
        # Calendar arithmetic over time(), which is already a libcall.
        if self.uses_datetime:
            wr('#include "mmb_datetime.h"\n')
        # A DATA table with no READ still calls mm_data_init5 from main.
        if self.data:
            self.uses_data = True
        if self.uses_data:
            wr('#include "mmb_data.h"\n')
        # The small pure families: GOSUB, BIT/BYTE/FLAG, BIN2STR$,
        # TRIM$/FIELD$ and the MAP() arithmetic.
        if self.uses_misc:
            wr('#include "mmb_misc.h"\n')
        if self.uses_pulse:
            wr('#include "mmb_pulse.h"\n')
        # The socket floor, then its families - before mmb_int.h,
        # whose network poll exists only under their include guards,
        # the mmb_sprite.h pattern.
        if self.uses_udp or self.uses_webclient or self.uses_webserver:
            self.uses_net = True
        if self.uses_net:
            wr('#include "mmb_net.h"\n')
        if self.uses_webclient:
            wr('#include "mmb_webc.h"\n')
        if self.uses_webserver:
            wr('#include "mmb_webs.h"\n')
        if self.uses_udp:
            wr('#include "mmb_udp.h"\n')
        # JSON$'s streaming walker: pure computation over the
        # program's own buffer, no net dependency.
        if self.uses_json:
            wr('#include "mmb_json.h"\n')
        # After mmb_gpio.h, which it uses to read the pins.  Only a
        # program that arms an interrupt carries any of it.
        if self.uses_interrupts:
            wr('#include "mmb_int.h"\n')
        if self.uses_pwm:
            wr('#include "mmb_pwm.h"\n')
        # Before both buses: it is the data forms they share.
        if self.uses_comms:
            wr('#include "mmb_comms.h"\n')
        if self.uses_i2c or self.uses_i2c0:
            wr('#include "mmb_i2c.h"\n')
        if self.uses_spi:
            wr('#include "mmb_spi.h"\n')
        if self.uses_peek:
            wr('#include "mmb_peek.h"\n')
        # LAST of the runtime headers, and it has to be: it services
        # whatever the others left behind, and finds them by their own
        # include guards.
        # TEMPR sleeps out a conversion of up to 750 ms, so it wants the
        # serviced wait for the same reason PAUSE does - and it wants it
        # even in a program that never says PAUSE, which is what the
        # uses_wait flag alone would miss.  Found on the board: a
        # SETTICK 100 handler fired ONCE during a 12-bit conversion.
        if self.uses_onewire and (self.uses_interrupts or self.uses_pulse):
            self.uses_wait = True
        if self.uses_wait:
            wr('#include "mmb_wait.h"\n')
        # AFTER mmb_wait.h, and that ordering is load-bearing: TEMPR
        # uses the serviced wait when the program has one, which it
        # detects by that header's guard.
        if self.uses_onewire:
            wr('#include "mmb_onewire.h"\n')
        # Pulsin( and Distance(: after mmb_gpio.h, whose pin modes and
        # register helpers they use, and after mmb_wait.h for mm_pause.
        if self.uses_pulsin:
            wr('#include "mmb_pulsin.h"\n')
        wr('#include <math.h>\n')
        wr('#include <string.h>\n')
        wr('#include <stdlib.h>\n\n')
        # PLAY VOLUME sets this and every later PLAY passes it on,
        # which is what makes the volume stick across statements the
        # way MMBasic's does.  Emitted only when the program plays
        # something, so nothing else carries it - the same bargain as
        # the two headers above.  mmb_play.h brings its own copy, so a
        # program with SOUND or TONE must not get a second.
        if self.uses_play and not self.uses_playd:
            wr('static int mm_play_volume = 80;\n\n')
        if self.type_order:
            wr('/* ---- TYPE definitions: the firmware layout, byte for'
               ' byte (TYPE-SPEC.md).\n')
            wr(' * Numeric members start 8-aligned, strings are packed,'
               ' a nested member\n')
            wr(' * always starts 8-aligned - the explicit pads carry the'
               ' difference\n')
            wr(' * where C alignment alone would not. ---- */\n')
            for tn in self.type_order:
                td = self.types[tn]
                wr('struct t_%s {\n' % tn)
                pos = 0
                padn = 0
                for m in td.members:
                    if m.offset > pos:
                        wr('    unsigned char __p%d[%d];\n'
                           % (padn, m.offset - pos))
                        padn += 1
                    if m.stype is not None:
                        decl = 'struct t_%s m_%s' % (m.stype, m.name)
                        if m.dims is not None:
                            decl += '[%d]' % m.count
                    elif m.ty == TY_S:
                        decl = 'char m_%s[%d]' % (m.name,
                                                  m.esize * m.count)
                    else:
                        decl = '%s m_%s' % (CTYPE[m.ty], m.name)
                        if m.dims is not None:
                            decl += '[%d]' % m.count
                    wr('    %s;\n' % decl)
                    pos = m.offset + m.esize * m.count
                wr('};    /* %d bytes */\n' % td.total)
            wr('\n')
        wr('/* ---- constants ---- */\n')
        names = list(self.globals.keys())
        names.sort()
        for nm in names:
            s = self.globals[nm]
            if s.is_const and not s.const_runtime:
                wr('#define %s %s\n' % (cconst(nm), s.acc))
        rt = [nm for nm in names
              if self.globals[nm].is_const
              and self.globals[nm].const_runtime]
        if rt:
            wr('/* CONSTs whose expressions need the runtime: hidden\n')
            wr(' * globals, assigned ONCE where each CONST stands -\n')
            wr(' * cmd_const evaluates at the statement, never at the\n')
            wr(' * use */\n')
            for nm in rt:
                s = self.globals[nm]
                if s.ty == TY_S:
                    wr('static char %s[MM_STRSZ];\n' % cconst(nm))
                else:
                    wr('static %s %s;\n' % (CTYPE[s.ty], cconst(nm)))
        wr('\n/* ---- global variables ---- */\n')
        for ln in self.global_decls():
            wr(ln + '\n')
        for ln in self.local_structs():
            wr(ln + '\n')
        if self.bnd_tables:
            wr('\n/* ---- array bounds tables (FCC has no compound'
               ' literals) ---- */\n')
            tabs = list(self.bnd_tables.values())
            tabs.sort()
            for name, body in tabs:
                wr('static const MMINTEGER %s[] = { %s };\n' % (name, body))
        if self.data:
            wr('\n/* ---- DATA items: parallel primitive arrays, so no\n')
            wr(' * struct layout crosses the bcrun VM boundary.\n')
            wr(' *\n')
            wr(' * A column whose kind no item has is not emitted at all -\n')
            wr(' * READ never looks at it, and eight bytes an item is worth\n')
            wr(' * having back: picofrog carries 1692 items and not one\n')
            wr(' * float, which was 13,536 bytes of zeroes in the data\n')
            wr(' * segment of a program that had none to spare. ---- */\n')
            if self.data_uniform_kind is None:
                wr('static const int __mmb_data_kind[] = {\n')
                for kind, f, i, sv in self.data:
                    wr('    %d,\n' % kind)
                wr('};\n')
            if self.data_has_f:
                wr('static const MMFLOAT __mmb_data_f[] = {\n')
                for kind, f, i, sv in self.data:
                    wr('    %s,\n' % f)
                wr('};\n')
            if self.data_has_i:
                wr('static const MMINTEGER __mmb_data_i[] = {\n')
                for kind, f, i, sv in self.data:
                    wr('    %s,\n' % i)
                wr('};\n')
            if self.data_has_s:
                wr('static const char *__mmb_data_s[] = {\n')
                for kind, f, i, sv in self.data:
                    wr('    %s,\n' % sv)
                wr('};\n')
        if self.uses_fbt:
            wr('\n/* FRAMEBUFFER LAYER\'s transparent colour: the merge\n')
            wr(' * default when MERGE names none (transparentlow/high\n')
            wr(' * in the firmware) */\n')
            wr('static int __mm_fbt = 0;\n')
        if self.uses_onerror:
            wr('\n/* ---- ON ERROR state, read by the guards below ---- *\n')
            wr(' * [0] is the poison: an error has been recorded and the\n')
            wr(' * rest of this statement is skipped.  [1] is the skip\n')
            wr(' * count, MMBasic\'s OptionErrorSkip: 0 abort, -1 ignore.\n')
            wr(' * It lives here rather than in the runtime so a guard is\n')
            wr(' * a load and a branch instead of a library call. */\n')
            wr('static int __mm_e[2];\n')
        if self.uses_i2c:
            # SETPIN puts the pins here and OPEN reads them: MMBasic
            # allows the two to be far apart in a program.
            wr('static int __mmi2c_sda, __mmi2c_scl;\n')
        if self.uses_spi:
            # the same for SPI's three, in whatever order they were
            # written - mmb_spi.h works out which pin is which signal
            wr('static int __mmspi_a, __mmspi_b, __mmspi_c;\n')
        # TRANSMIT PAGE's expression tables, one per call site: the
        # normalised texts mm_webpg_next matches braces against.
        # Emitted before the routine bodies that reference them.
        for tno, keys in enumerate(self.websubs):
            wr('\n/* WEB TRANSMIT PAGE call site %d: %d expressions '
               '*/\n' % (tno, len(keys)))
            wr('static const char *const __mmwebsub_%d[%d] = {\n'
               % (tno, max(1, len(keys))))
            if not keys:
                wr('    "",\n')
            for k in keys:
                wr('    "%s",\n'
                   % k.replace('\\', '\\\\').replace('"', '\\"'))
            wr('};\n')
        # DefineFont: the glyphs live in this program's image and the
        # kernel is given their address, so they are `static const' -
        # nothing copies them and nothing may move them.
        for fno in sorted(self.fonts):
            data = self.fonts[fno]
            wr('\n/* DefineFont %d: %dx%d, %d characters from %d, '
               '%d bytes */\n'
               % (fno, data[0], data[1], data[3], data[2], len(data)))
            wr('static const unsigned char __mmfont_%d[%d] = {\n'
               % (fno, len(data)))
            for off in range(0, len(data), 12):
                row = data[off:off + 12]
                wr('    ' + ','.join('0x%02x' % b for b in row) + ',\n')
            wr('};\n')
        if self.uses_fbsel:
            # FRAMEBUFFER's buffer as a string the program computes -
            # MMBasic's getCstring/strcasecmp arm.  Emitted here rather
            # than put in a header because it needs nothing but the
            # runtime every program already has, and a header would be
            # one more file to keep in step on the board.
            wr('\n/* FRAMEBUFFER buffer named at run time */\n'
               'static int __mmb_fbsel(const char *s)\n'
               '{\n'
               '    int c = mm_slen(s) == 1 ? mm_cstr(s)[0] : 0;\n'
               '    if (c >= \'a\' && c <= \'z\')\n'
               '        c -= 32;\n'
               '    if (c == \'N\') return 0;\n'
               '    if (c == \'F\') return 1;\n'
               '    if (c == \'L\') return 2;\n'
               '    mm_error("expected N, F or L");\n'
               '    return 0;\n'
               '}\n')
        wr('\n/* ---- forward declarations ---- */\n')
        if self.uses_clear:
            wr('static void __mmb_clear(void);\n')
        rn = list(self.routines.keys())
        rn.sort()
        for nm in rn:
            wr(self.signature(self.routines[nm]) + ';\n')
        for d in self.call_disp:
            wr(self.calld_head(d) + ';\n')
        if self.uses_clear:
            wr('\nstatic void __mmb_clear(void)\n{\n')
            names = list(self.globals.keys())
            names.sort()
            for nm in names:
                sym = self.globals[nm]
                if sym.is_const:
                    continue
                wr('    ' + self.zero_of(sym) + '\n')
            wr('}\n')
        wr('\n/* ---- subroutines and functions ---- */\n')
        for ln in self.out_body:
            wr(ln + '\n')
        if self.call_disp:
            wr('\n/* ---- CALL by name ---- */\n')
            wr('/* One dispatcher per argument shape: the name is\n')
            wr(' * compared case-insensitively, with and without its\n')
            wr(' * type suffix, against every routine that could take\n')
            wr(' * these arguments. */\n')
            wr('static int __mm_nameeq(const char *m, const char *lit)\n')
            wr('{\n')
            wr('    int n = (unsigned char)m[0];\n')
            wr('    int i;\n')
            wr('    for (i = 0; i < n; i++) {\n')
            wr('        char ca = m[1 + i], cb = lit[i];\n')
            wr('        if (cb == 0) return 0;\n')
            wr("        if (ca >= 'a' && ca <= 'z') ca -= 32;\n")
            wr("        if (cb >= 'a' && cb <= 'z') cb -= 32;\n")
            wr('        if (ca != cb) return 0;\n')
            wr('    }\n')
            wr('    return lit[n] == 0;\n')
            wr('}\n')
            sfx = {TY_S: '$', TY_I: '%', TY_F: '!'}
            for d in self.call_disp:
                rep = d['rep']
                fwd = []
                if d['is_func'] and rep.ty == TY_S:
                    fwd.append('__ret')
                for p in rep.params[:d['nargs']]:
                    fwd.append('p_' + p.name.replace('.', '__'))
                wr('\n%s\n{\n' % self.calld_head(d))
                for r in d['cands']:
                    names = [r.name]
                    if r.is_func:
                        names.append(r.name + sfx[r.ty])
                    cond = ' || '.join('__mm_nameeq(__nm, "%s")' % n2
                                       for n2 in names)
                    # a candidate's spare parameters take their defaults
                    tail = [self.pass_arg(p, None, r)
                            for p in r.params[d['nargs']:]]
                    callx = '%s(%s)' % (r.cname, ', '.join(fwd + tail))
                    if d['is_func']:
                        wr('    if (%s) return %s;\n' % (cond, callx))
                    else:
                        wr('    if (%s) { %s; return; }\n' % (cond, callx))
                wr('    mm_error("CALL: no such SUB or FUNCTION");\n')
                if d['is_func'] and rep.ty == TY_S:
                    wr('    return __ret;\n')
                elif d['is_func']:
                    wr('    return 0;\n')
                wr('}\n')
        wr('\n/* ---- main program ---- */\n')
        # MAIN TAKES NO ARGUMENTS IN THE FCC BUILD.
        #
        # bcrun dispatches the entry with nothing pushed - run() calls
        # bc_exec(h_entry) - so a main declared with two parameters
        # reads a frame two slots short and the program segfaults before
        # its first statement.  Nothing had ever caught it because no
        # test used MM.CMDLINE$, the only thing that asked for the
        # arguments, until MM.INFO(PATH) and MM.INFO(CURRENT) did.
        #
        # bcrun already holds the real command line and hands it over
        # inside its mm_argv_bind wrapper (bcrun_mm.c: it ignores what
        # the caller passed and uses prog_argv), so the arguments are
        # not lost - only the signature has to go.  The hosted gcc build
        # has no such wrapper and keeps the real one.
        if self.uses_cmdline and not self.fcc:
            wr('int main(int argc, char **argv)\n{\n')
        else:
            wr('int main(void)\n{\n')
        wr('    unsigned __mark = mm_mark(); (void)__mark;\n')
        # Fonts first, and here rather than where the block sits: the
        # interpreter binds them at LOAD, so a program may select font
        # 10 a thousand lines above its DefineFont.
        for fno in sorted(self.fonts):
            wr('    mm_fontdef(%d, (MMINTEGER)(long)__mmfont_%d, '
               '(MMINTEGER)sizeof __mmfont_%d);\n' % (fno, fno, fno))
        if self.uses_cmdline:
            wr('    mm_argv_bind(argc, argv);\n' if not self.fcc
               else '    mm_argv_bind(0, (char **)0);\n')
        if self.uses_onerror:
            wr('    mm_err_bind(__mm_e);\n')
        if getattr(self, 'heap_used', False):
            wr('    H = mm_heap(sizeof *H);   /* arrays and strings */\n')
        if self.data:
            uk = self.data_uniform_kind
            wr('    mm_data_init5(%s, %d, %s, %s, %s, %d);\n'
               % ('__mmb_data_kind' if uk is None else '0',
                  0 if uk is None else uk,
                  '__mmb_data_f' if self.data_has_f else '0',
                  '__mmb_data_i' if self.data_has_i else '0',
                  '__mmb_data_s' if self.data_has_s else '0',
                  len(self.data)))
        for ln in self.out_main:
            wr(ln + '\n')
        wr('    return 0;\n}\n')


# ----------------------------------------------------------------- driver

def _const_fixup(conv):
    """CONST names are emitted as #define, so their access text must be the
    macro name, not the expression."""
    for nm in conv.globals:
        s = conv.globals[nm]
        if s.is_const:
            s.acc = cconst(nm)


def _heap_fixup(conv):
    """Arrays and strings live in the struct, so every reference to one
    goes through H.

    The emitter reads s.acc for every read and write of a variable, so
    rewriting it here IS the change - nothing in the expression or
    statement code has to know.  Done between the scan and emit passes,
    the same way _const_fixup rewrites CONST access text."""
    for nm in conv.globals:
        s = conv.globals[nm]
        if s.is_const:
            continue
        if s.is_array or s.ty == TY_S or s.stype is not None:
            s.acc = 'H->' + cvar(nm)
            if s.dynamic:
                s.bacc = 'H->__b_' + cvar(nm)


def _local_heap_fixup(conv):
    """LOCAL arrays and strings live in a block taken per invocation, so
    every reference to one goes through __L.

    The same rewrite _heap_fixup does for globals, per routine: the
    emitter reads s.acc for every read and write, so setting it here is
    the change.  Recursion is then correct for free - each invocation
    allocates its own block and frees it on the way out, which is what
    MMBasic does with LOCALs and what the C stack was doing for the
    scalars all along.

    STATIC locals keep their C storage: they outlive the invocation by
    definition, so a per-invocation block is exactly the wrong home.
    Parameters are the caller's, passed in, and are not ours to move.
    """
    for r in conv.routines.values():
        heap = [nm for nm in r.local_order
                if not r.locals[nm].is_param
                and not r.locals[nm].is_static
                and (r.locals[nm].is_array or r.locals[nm].ty == TY_S
                     or r.locals[nm].stype is not None)]
        if not heap:
            continue
        r.heap_locals = True
        for nm in heap:
            r.locals[nm].acc = '__L->' + cvar(nm)
            if r.locals[nm].dynamic:
                r.locals[nm].bacc = '__L->__b_' + cvar(nm)


def convert(inpath, outpath=None, report=False, lenient=True, fcc=False):
    lines = []
    f = open(inpath, 'r')
    try:
        while True:
            ln = f.readline()
            if not ln:
                break
            # XMODEM pads the last block, so a program that arrived that
            # way carries NULs after its last line - brownian.bas came
            # over with 23 of them after "End Sub".  That is how files
            # reach these machines, and MMBasic reads such a file
            # without noticing, its program store being NUL-terminated.
            # Refusing it meant a program that runs on the reference
            # would not translate here, and said so at a line number
            # past the end of the program.
            #
            # NUL is where the program stops, which is what it means in
            # a BASIC source - not something to strip out of the middle.
            if '\0' in ln:
                ln = ln.split('\0', 1)[0]
                if ln:
                    lines.append(ln)
                break
            lines.append(ln)
    finally:
        f.close()

    conv = Conv(lines, inpath)
    conv.lenient = lenient
    conv.fcc = fcc
    # before every other pass: a DefineFont block is hex, not BASIC, and
    # is in force from the top of the program however far down it sits
    conv.pass_fonts()
    conv.pass_routine_names()
    conv.pass_types()
    conv.pass_declarations()
    conv.walk('scan')
    # constants become #define, so fix their access text before emitting
    consts = {}
    for nm in conv.globals:
        if conv.globals[nm].is_const:
            consts[nm] = conv.globals[nm].acc
    for nm in consts:
        conv.globals[nm].acc = cconst(nm)
    _heap_fixup(conv)
    _local_heap_fixup(conv)
    conv.tmpn = 0
    conv.out_main = []
    conv.out_body = []
    conv.walk('emit')
    # restore expression text for the #define bodies
    for nm in consts:
        conv.globals[nm].acc = consts[nm]

    if conv.errors:
        for e in conv.errors:
            print('ERROR ' + e)
        return None

    if outpath is None:
        if inpath.lower().endswith('.bas'):
            outpath = inpath[:-4] + '.c'
        else:
            outpath = inpath + '.c'
    of = open(outpath, 'w')
    try:
        conv.write(of.write)
    finally:
        of.close()

    if conv.skipped:
        print("%d line(s) could not be translated and were commented out:"
              % len(conv.skipped))
        for ln, text, why in conv.skipped:
            # The offending source, not just the complaint.  It was
            # already being kept for the report at the end of the C and
            # simply never shown here, so a message like "expected ')'"
            # arrived with nothing to attach it to - on a board, where
            # the source is a file away, that is most of the diagnosis
            # missing.
            print("  line %d: %s" % (ln, why))
            if text:
                print("      %s" % text[:72])
    if report:
        for ln in conv.report():
            print(ln)
    for w in conv.warnings:
        print('warning: ' + w)
    return outpath


def dump_tokens(inpath):
    """Debug aid for the C rewrite (mmbc): print the token stream in a
    fixed format so `mmbc --tokens` can be byte-diffed against it."""
    f = open(inpath, 'r')
    lines = f.readlines()
    f.close()
    scan_escape(lines)
    for idx in range(len(lines)):
        lineno = idx + 1
        try:
            toks = tokenize(lines[idx], lineno)
        except MMError as e:
            print('ERR %d %s' % (lineno, str(e)))
            continue
        for kind, text, up in toks:
            print('%d %d [%s] [%s]' % (lineno, kind, text, up))
    return 0


def main(argv):
    src = None
    dst = None
    rep = False
    strict = False
    fcc = False
    tokens = False
    k = 1
    while k < len(argv):
        a = argv[k]
        if a == '-o':
            k += 1
            dst = argv[k]
        elif a == '--report':
            rep = True
        elif a == '--strict':
            strict = True
        elif a == '--fcc':
            fcc = True
        elif a == '--gcc':
            # the default here, and the board build's opposite: mmbc on
            # the PC3 translates for the Fuzix compiler unless asked
            fcc = False
        elif a == '--tokens':
            tokens = True
        elif a in ('-h', '--help'):
            print("usage: mmb2c.py source.bas [-o out.c] [--report] "
                  "[--strict] [--fcc]")
            print("  --report  list implied globals and skipped lines")
            print("  --strict  stop on anything that cannot be translated,")
            print("            instead of commenting it out and carrying on")
            print("  --fcc     C89 output for the Fuzix C compiler: no")
            print("            compound literals")
            return 0
        else:
            src = a
        k += 1
    if src is None:
        print("usage: mmb2c.py source.bas [-o out.c] [--report] [--strict] "
              "[--fcc]")
        return 1
    if tokens:
        return dump_tokens(src)
    out = convert(src, dst, rep, not strict, fcc)
    if out is None:
        return 2
    print("wrote " + out)
    return 0


if __name__ == '__main__':
    # On a bare-metal MicroPython port there is no sys.argv, so importing
    # the module simply makes convert() available at the REPL:
    #     import mmb2c ; mmb2c.convert("prog.bas", "prog.c", True)
    _argv = None
    try:
        import sys
        _argv = getattr(sys, 'argv', None)
    except ImportError:
        _argv = None
    if _argv:
        raise SystemExit(main(_argv))

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
    'DATE$': (0, 0), 'TIME$': (0, 0), 'CWD$': (0, 0),
    'EOF': (1, 1), 'LOC': (1, 1), 'LOF': (1, 1), 'INPUT$': (2, 2),
    # these are parsed by hand because an argument may be a bare keyword
    'CHOICE': (3, 3), 'BOUND': (1, 2), 'TRIM$': (1, 3), 'FIELD$': (2, 4),
    'DATETIME$': (1, 1), 'DAY$': (1, 1), 'EPOCH': (1, 1),
    'BIN2STR$': (2, 3), 'STR2BIN': (2, 3), 'RGB': (1, 3), 'MATH': (1, 1),
    'DIR$': (0, 2),
    'LLEN': (1, 1), 'LGETSTR$': (3, 3), 'LGETBYTE': (2, 2),
    'LINSTR': (2, 3), 'LCOMPARE': (2, 2), 'LINPUT': (3, 3),
}

# built-ins whose arguments cannot be parsed as plain expressions
RAWARG = ('CHOICE', 'BOUND', 'TRIM$', 'DATETIME$', 'DAY$', 'EPOCH',
          'BIN2STR$', 'STR2BIN', 'RGB', 'MATH',
          'EOF', 'LOC', 'LOF', 'INPUT$', 'DIR$',
          'LLEN', 'LGETSTR$', 'LGETBYTE', 'LINSTR', 'LCOMPARE', 'LINPUT')

# built-ins that return a string (and therefore consume a scratch buffer)
STRFUNCS = ('CHR$', 'LEFT$', 'RIGHT$', 'MID$', 'STR$', 'HEX$', 'OCT$',
            'BIN$', 'UCASE$', 'LCASE$', 'SPACE$', 'STRING$', 'LTRIM$',
            'RTRIM$', 'TAB', 'FORMAT$', 'TRIM$', 'FIELD$', 'DATE$',
            'TIME$', 'DATETIME$', 'DAY$', 'BIN2STR$', 'INPUT$', 'DIR$',
            'CWD$', 'LGETSTR$')

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

OPS3 = ()
OPS2 = ('<=', '>=', '<>', '=<', '=>', '><', '<<', '>>')
OPS1 = '+-*/\\^=<>(),;:?@#'


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


def cblock_safe(text):
    """Make text safe to sit inside a C /* */ comment."""
    out = text.replace('*/', '* /')
    return ''.join(c if 32 <= ord(c) < 127 else ' ' for c in out)


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
            out.append((T_STR, ''.join(buf), ''))
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
                 'declared_in', 'disp', 'has_init')

    def __init__(self, name, ty, acc):
        self.name = name          # canonical: lower case, no suffix
        self.disp = name          # as the programmer spelled it
        self.ty = ty
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


class Routine(object):
    __slots__ = ('name', 'cname', 'is_func', 'ty', 'params', 'locals',
                 'statics', 'line', 'gtouch', 'disp', 'local_order')

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


def cvar(name):
    return 'v_' + name.replace('.', '__')


def clabel(name):
    return 'L_' + name.replace('.', '__')


# ----------------------------------------------------------------- the guts

class Conv(object):

    def __init__(self, lines, srcname='program'):
        self.lines = lines            # list of source lines
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

    # -- error reporting ------------------------------------------------
    def err(self, msg):
        raise MMError("line %d: %s" % (self.lineno, msg))

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
            self.err("expected '%s'" % s)

    def accept_kw(self, s):
        if self.is_kw(s):
            self.i += 1
            return True
        return False

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

    def raw(self, text):
        if self.mode != 'emit':
            return
        self.out.append(text)

    def newtmp(self, pfx):
        self.tmpn += 1
        return '__%s%d' % (pfx, self.tmpn)

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
        self.err("string used where a number is required")

    def as_flt(self, v):
        code, ty = v
        if ty == TY_F:
            return code
        if ty == TY_I:
            return '(MMFLOAT)(' + code + ')'
        self.err("string used where a number is required")

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
                v = ('((%s) %s (%s) ? 1 : 0)' % (v[0], cop, r[0]), TY_I)

    def e_unary_not(self):
        t = self.peek()
        if t is not None and t[0] == T_ID and t[2] in ('NOT', 'INV'):
            self.i += 1
            v = self.e_unary_not()
            if t[2] == 'NOT':
                return ('((%s) == 0 ? 1 : 0)' % self.as_flt(v), TY_I)
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
                    v = ('((%s) / (%s))'
                         % (self.as_flt(v), self.as_flt(r)), TY_F)
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

        if up in BUILTINS and (BUILTINS[up][0] == 0 or self.is_op('(')):
            return self.call_builtin(up)

        as_array = self.is_op('(')
        s = self.reference(word, as_array)
        if as_array:
            if not s.is_array:
                self.err("'%s' is not an array" % canon)
            return (self.index(s), s.ty)
        if s.is_const:
            return (s.acc, s.ty)
        if s.is_array:
            self.err("array '%s' used without an index" % canon)
        return (s.acc, s.ty)

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
        if s.is_param:
            # MMBasic gives an array parameter no rank of its own - it
            # inherits whatever was passed - so the subscripts are folded
            # into one offset using the bounds handed in alongside it.
            b = '__b_' + s.name.replace('.', '__')
            off = parts[0]
            for k in range(1, len(parts)):
                off = '((%s) * ((%s)[%d] + 1) + (%s))' % (off, b, k + 1,
                                                          parts[k])
            return '%s[%s]' % (s.acc, off)
        if len(parts) != len(s.dims):
            self.err("'%s' has %d dimension(s), %d given"
                     % (s.name, len(s.dims), len(parts)))
        return s.acc + ''.join('[' + p + ']' for p in parts)

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
        v = self.expr()
        return ('val', None, v)

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
        if p.is_array:
            if a is None:
                self.err("array argument to '%s' cannot be omitted" % r.name)
            if a[0] != 'array':
                self.err("'%s' expects a whole array here" % r.name)
            s = a[1]
            if s.ty != p.ty:
                self.err("array type mismatch in call to '%s'" % r.name)
            if s.is_param:
                bnd = '__b_' + s.name.replace('.', '__')
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
            cf = {'SQR': 'sqrt', 'SIN': 'sin', 'COS': 'cos', 'TAN': 'tan',
                  'ATN': 'atan', 'LOG': 'log', 'EXP': 'exp',
                  'ASIN': 'asin', 'ACOS': 'acos'}[up]
            return ('%s(%s)' % (cf, f(0)), TY_F)
        if up == 'ATAN2':
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
        if up == 'LEN':
            return ('(MMINTEGER)mm_slen(%s)' % s(0), TY_I)
        if up == 'ASC':
            return ('mm_asc(%s)' % s(0), TY_I)
        if up == 'BYTE':
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
            return ('mm_timer()', TY_I)
        if up == 'DATE$':
            return ('mm_date_str()', TY_S)
        if up == 'TIME$':
            return ('mm_time_str()', TY_S)
        if up == 'CWD$':
            return ('mm_cwd()', TY_S)
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
            return ('mm_field(%s, %s, %s, %s)'
                    % (s(0), n(1), delim, quote), TY_S)
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
            return ('mm_trim(%s, %s, %s)' % (src[0], mask, where), TY_S)

        if up in ('DATETIME$', 'DAY$', 'EPOCH'):
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

        if up in ('LLEN', 'LGETSTR$', 'LGETBYTE', 'LINSTR', 'LCOMPARE',
                  'LINPUT'):
            self.expect_op('(')
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

        if up == 'MATH':
            self.expect_op('(')
            t = self.nxt()
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
                if name in ('MAX', 'MIN'):
                    return ('mm_st_%s_%s(%s, %s, %s)'
                            % (fn, sfx, ptr, cnt, idx), TY_F)
                return ('mm_st_%s_%s(%s, %s)' % (fn, sfx, ptr, cnt), TY_F)
            if t[0] != T_ID or t[2] not in MATHFUNCS:
                self.err("MATH(%s ...) is not supported; translated are "
                         "%s and the array reductions %s"
                         % (t[1], ', '.join(sorted(MATHFUNCS)),
                            ', '.join(MATHARRAY)))
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

    def array_flat(self, s):
        """(pointer to element 0, element count) for a whole array."""
        if not s.is_array:
            self.err("'%s' is not an array" % s.name)
        if s.is_param:
            cnt = 'mm_arr_count(__b_%s)' % s.name.replace('.', '__')
            return (s.acc, cnt)
        cnt = '(int)(%s)' % ' * '.join('(%s)' % d for d in s.dims)
        if s.ty == TY_S:
            return ('(char (*)[MM_STRSZ])%s' % s.acc, cnt)
        return ('(%s *)%s' % (CTYPE[s.ty], s.acc), cnt)

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
        parameter carries its bounds in a hidden extra argument."""
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
        if sym.is_param:
            nm = '__b_' + sym.name.replace('.', '__')
            if k == 0:
                return str(self.opt_base)
            return '(%s)[%s]' % (nm, kexpr)
        if k is None:
            self.err("BOUND() on a DIMmed array needs a constant dimension")
        if k == 0:
            return str(self.opt_base)
        if k > len(sym.dims):
            return '0'
        return '((%s) - 1)' % sym.dims[k - 1]


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

    def pass_declarations(self):
        self.mode = 'decl'
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
                and t[2] not in KEYWORDS and t[2] not in BUILTINS:
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
        if self.accept_kw('AS'):
            ty2 = self.type_word()
            if ty is not None and ty != ty2:
                self.err("parameter type conflict")
            ty = ty2
        if ty is None:
            ty = self.opt_default
        s = Sym(canon, ty, '')
        s.is_param = True
        s.byref = byref
        s.where = self.lineno
        s.declared_in = r.name
        if dims is not None:
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

    def do_option(self):
        t = self.peek()
        if t is None:
            return
        if t[2] == 'DEFAULT':
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
            if self.accept_op('('):
                dims = []
                while True:
                    v = self.expr()
                    dims.append('(%s) + 1' % self.as_int(v))
                    if not self.accept_op(','):
                        break
                self.expect_op(')')
            ty = sfx if sfx is not None else group_ty
            if self.accept_kw('AS'):
                ty2 = self.type_word()
                if ty is not None and ty != ty2:
                    self.err("conflicting types for '%s'" % canon)
                ty = ty2
            if ty is None:
                ty = self.opt_default
            if ty is None:
                self.err("OPTION DEFAULT NONE: '%s' needs a type" % canon)

            if self.mode == 'decl':
                s = self.declare(canon, ty, scope, dims, static)
            else:
                s = self.lookup(canon)

            if self.accept_op('='):
                if s is not None:
                    s.has_init = True
                self.emit_initialiser(s, static)

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
                        self.emit('mm_sset(%s, %s);' % (sub, v[0]))
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
        """Element k of an array in an initialiser list."""
        if len(s.dims) == 1:
            return '%s[%d]' % (s.acc, k)
        self.err("initialiser lists are only supported for 1-D arrays")

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
            if self.mode == 'decl':
                s = Sym(canon, ty, '(' + v[0] + ')')
                s.is_const = True
                s.where = self.lineno
                if canon in self.globals:
                    self.err("'%s' already declared" % canon)
                self.globals[canon] = s
            if not self.accept_op(','):
                break

    # ==================================================================
    #  passes 2 and 3 - walk every statement, scanning then emitting
    # ==================================================================

    def walk(self, mode):
        self.mode = mode
        self.gosub_n = 0
        self.cur = None
        self.indent = 1
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
        if up == 'CLEAR':
            self.i += 1
            self.warn("CLEAR zeroes every global; static storage cannot be "
                      "handed back the way the interpreter does")
            self.emit('__mmb_clear();')
            self.uses_clear = True
            return
        if up == 'PAUSE':
            self.i += 1
            v = self.expr()
            self.emit('mm_pause(%s);' % self.as_flt(v))
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
            w = self.peek()
            kw = w[2] if (w is not None and w[0] == T_ID) else ''
            self.skip_statement()
            if kw in ('IGNORE', 'SKIP', 'RESTART'):
                self.err("ON ERROR %s needs soft error handling, which is "
                         "not in yet; only ABORT and CLEAR are translated"
                         % kw)
            return
        if up == 'ON' and self.peek(1) is not None \
                and not self.is_kw('ERROR', 1) and not self.is_kw('KEY', 1) \
                and not self.is_kw('PS2', 1):
            self.i += 1
            self.do_on_goto()
            return
        if up == 'ARRAY':
            self.i += 1
            self.do_array_cmd()
            return
        if up == 'MATH' and not self.is_op('(', 1):
            self.i += 1
            self.do_array_cmd()
            return
        if up in ('TIMER', 'DATE$', 'TIME$') and self.is_op('=', 1):
            self.i += 2
            v = self.expr()
            if up == 'TIMER':
                self.emit('mm_timer_set(%s);' % self.as_int(v))
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
        while not self.stmt_end():
            if self.accept_op(';'):
                suppress_nl = True
                continue
            if self.accept_op(','):
                self.emit(self.prcall(chan, 'tab', None))
                suppress_nl = True
                continue
            v = self.expr()
            suppress_nl = False
            if v[1] == TY_S:
                self.emit(self.prcall(chan, 's', v[0]))
            elif v[1] == TY_I:
                self.emit(self.prcall(chan, 'i', v[0]))
            else:
                self.emit(self.prcall(chan, 'f', v[0]))
        if not suppress_nl:
            self.emit(self.prcall(chan, 'nl', None))

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
                b = '-1'
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
        self.emit('mm_gosub_push(%d); goto %s;' % (site, clabel(canon)))
        self.raw('__GR%d: ;' % site)

    def do_return(self):
        sites = self.gosub_sites.get(self.gosub_key(), [])
        if not sites:
            self.err("RETURN without any GOSUB in this part of the program")
        self.emit('switch (mm_gosub_pop()) {')
        for site in sites:
            self.emit('    case %d: goto __GR%d;' % (site, site))
        self.emit('    default: mm_error("RETURN without GOSUB");')
        self.emit('}')

    # -- DATA / READ / RESTORE ---------------------------------------------
    def do_read(self):
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
                    self.emit('    mm_sset((%s)[%s], mm_read_s()); }'
                              % (ptr, k))
                else:
                    self.emit('    (%s)[%s] = mm_read_%s(); }'
                              % (ptr, k, 'i' if sym.ty == TY_I else 'f'))
                self.tmp_used = True
            else:
                tgt, ty = self.input_target()
                if ty == TY_S:
                    self.emit('mm_sset(%s, mm_read_s());' % tgt)
                    self.tmp_used = True
                else:
                    self.emit('%s = mm_read_%s();'
                              % (tgt, 'i' if ty == TY_I else 'f'))
            if not self.accept_op(','):
                break

    def do_restore(self):
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
        self.emit('mm_sort_%s(%s, %s, %s, (int)(%s), (int)(%s), (int)(%s));'
                  % (kind, ptr, idx, cnt, start, count, flags))

    # -- INC / CAT / ERASE ---------------------------------------------------
    def do_inc(self):
        tgt, ty = self.input_target()
        if self.accept_op(','):
            v = self.expr()
        else:
            v = ('1LL', TY_I)
        if ty == TY_S:
            if v[1] != TY_S:
                self.err("INC on a string needs a string increment")
            self.emit('mm_sset(%s, mm_scat(%s, %s));' % (tgt, tgt, v[0]))
            self.tmp_used = True
        elif ty == TY_I:
            self.emit('%s += %s;' % (tgt, self.as_int(v)))
        else:
            self.emit('%s += %s;' % (tgt, self.as_flt(v)))

    def do_cat(self):
        tgt, ty = self.input_target()
        if ty != TY_S:
            self.err("CAT needs a string variable")
        self.expect_op(',')
        v = self.expr()
        if v[1] != TY_S:
            self.err("CAT needs a string to append")
        self.emit('mm_sset(%s, mm_scat(%s, %s));' % (tgt, tgt, v[0]))
        self.tmp_used = True

    def do_erase(self):
        self.warn("ERASE zeroes the variable; static storage cannot be "
                  "handed back the way the interpreter does")
        while not self.stmt_end():
            t = self.peek()
            if t is None or t[0] != T_ID:
                self.err("ERASE needs a variable name")
            sym = self.reference(t[1], False)
            self.i += 1
            if self.accept_op('('):
                self.expect_op(')')
            self.emit(self.zero_of(sym))
            if not self.accept_op(','):
                break

    def zero_of(self, sym):
        if sym.is_array:
            ptr, cnt = self.array_flat(sym)
            if sym.ty == TY_S:
                return 'mm_arr_set_s(%s, %s, "\\000" "");' % (ptr, cnt)
            return 'mm_arr_set_%s(%s, %s, 0);' % (
                'i' if sym.ty == TY_I else 'f', ptr, cnt)
        if sym.ty == TY_S:
            return '%s[0] = 0; %s[1] = 0;' % (sym.acc, sym.acc)
        return '%s = 0;' % sym.acc

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
    def do_array_cmd(self):
        t = self.nxt()
        if t[0] != T_ID:
            self.err("ARRAY/MATH needs a sub-command")
        op = t[2]
        if op == 'SET':
            val = self.expr()
            self.expect_op(',')
            sym = self.arrayref()
            ptr, cnt = self.array_flat(sym)
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
            src = self.arrayref()
            self.expect_op(',')
            val = self.expr()
            self.expect_op(',')
            dst = self.arrayref()
            if src.ty != dst.ty:
                self.err("%s needs both arrays to be the same type" % op)
            sptr, scnt = self.array_flat(src)
            dptr, dcnt = self.array_flat(dst)
            if src.ty == TY_S:
                if op == 'SCALE':
                    self.err("SCALE does not apply to a string array")
                if val[1] != TY_S:
                    self.err("a string array needs a string value")
                self.emit('mm_arr_add_s(%s, %s, %s, %s);'
                          % (sptr, scnt, val[0], dptr))
                return
            fn = 'mm_arr_%s_%s' % (op.lower(),
                                   'i' if src.ty == TY_I else 'f')
            conv = self.as_int if src.ty == TY_I else self.as_flt
            self.emit('%s(%s, %s, %s, %s);'
                      % (fn, sptr, scnt, conv(val), dptr))
            return
        if op == 'RANDOMIZE':
            if self.stmt_end():
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
        """A variable, possibly an array element, that INPUT can write."""
        t = self.nxt()
        if t[0] != T_ID:
            self.err("INPUT needs a variable")
        is_arr = self.is_op('(')
        sym = self.reference(t[1], False)
        if sym.is_const:
            self.err("'%s' is a CONST" % sym.name)
        if is_arr:
            if not sym.is_array:
                self.err("'%s' is not an array" % sym.name)
            return (self.index(sym), sym.ty)
        if sym.is_array:
            self.err("cannot INPUT into a whole array")
        return (sym.acc, sym.ty)

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
            tgt, ty = self.input_target()
            if ty == TY_S:
                self.emit('mm_sset(%s, mm_input_next());' % tgt)
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
        tgt, ty = self.input_target()
        if ty != TY_S:
            self.err("LINE INPUT needs a string variable")
        self.emit('mm_sset(%s, mm_getline(%s));' % (tgt, chan))
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

    def do_callstmt(self):
        t = self.peek()
        if t is not None and t[0] == T_STR:
            self.i += 1
            canon = t[1].lower()
        else:
            t = self.nxt()
            canon = split_suffix(t[1])[0]
        r = self.routines.get(canon)
        if r is None:
            self.err("CALL to unknown subroutine '%s'" % canon)
        self.accept_op(',')
        args = self.call_args(False)
        code, ty = self.emit_call(r, args)
        self.emit('%s;' % code)

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
                     self.as_int(num) if num else '-1', v[0]))

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
                self.emit('__ret = %s;' % self.as_int(v))
            else:
                self.emit('__ret = %s;' % self.as_flt(v))
            return

        is_arr = self.is_op('(')
        s = self.reference(t[1], False)
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
            self.emit('mm_sset(%s, %s);' % (target, v[0]))
        elif s.ty == TY_I:
            self.emit('%s = %s;' % (target, self.as_int(v)))
        else:
            self.emit('%s = %s;' % (target, self.as_flt(v)))

    # -- IF ---------------------------------------------------------------
    def cond(self):
        v = self.expr()
        if v[1] == TY_S:
            self.err("a string cannot be used as a condition")
        return '(%s) != 0' % v[0]

    def do_if(self):
        c = self.cond()
        if not self.accept_kw('THEN'):
            if self.is_kw('GOTO'):
                pass
            else:
                self.err("IF without THEN")
        if self.stmt_end():
            # block IF
            self.emit('if (%s) {' % c)
            self.indent += 1
            self.blocks.append(['if', self.lineno])
            return
        # single line IF
        self.emit('if (%s) {' % c)
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
        while not self.at_end() and not self.is_kw('ELSE'):
            if self.accept_op(':'):
                continue
            self.statement()
            if len(self.blocks) != depth:
                self.err("a single line IF cannot open a multi-line block")

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
            self.emit('mm_release(__mark); return;')
            return
        if self.accept_kw('FUNCTION'):
            self.emit('mm_release(__mark); return __ret;')
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
                    self.emit('mm_release(__mark); return __ret;')
                else:
                    self.emit('mm_release(__mark); return;')
                return
            self.err("bare EXIT is outside any loop, SUB or FUNCTION")
        self.err("unknown EXIT variant")

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
        self.blocks.append(['routine', self.lineno])

    def emit_local_decl(self, s):
        pfx = 'static ' if s.is_static else ''
        if s.is_static and s.has_init:
            self.emit('static int __once_%s = 0;'
                      % s.name.replace('.', '__'))
        if s.is_array:
            dims = ''.join('[%s]' % d for d in s.dims)
            if s.ty == TY_S:
                self.emit('%schar %s%s[MM_STRSZ];' % (pfx, s.acc, dims))
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

    def signature(self, r):
        parts = []
        if r.is_func and r.ty == TY_S:
            parts.append('char *__ret')
        for p in r.params:
            nm = 'p_' + p.name.replace('.', '__')
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
            self.emit('mm_release(__mark); return __ret;')
        else:
            self.emit('mm_release(__mark);')
        self.indent = 0
        self.raw('}')
        self.cur = None
        self.out = self.out_main
        self.indent = 1

    # ==================================================================
    #  driving it all
    # ==================================================================

    def run(self):
        self.pass_routine_names()
        self.pass_declarations()
        self.walk('scan')
        self.tmpn = 0
        self.out_main = []
        self.out_body = []
        self.walk('emit')
        return not self.errors

    def global_decls(self):
        out = []
        names = list(self.globals.keys())
        names.sort()
        for nm in names:
            s = self.globals[nm]
            if s.is_const:
                continue
            note = ''
            if s.implied:
                note = '   /* implied, first seen line %d */' % s.where
            if s.is_array:
                dims = ''.join('[%s]' % d for d in s.dims)
                if s.ty == TY_S:
                    out.append('char %s%s[MM_STRSZ];%s' % (s.acc, dims, note))
                else:
                    out.append('%s %s%s;%s'
                               % (CTYPE[s.ty], s.acc, dims, note))
            elif s.ty == TY_S:
                out.append('char %s[MM_STRSZ];%s' % (s.acc, note))
            else:
                out.append('%s %s;%s' % (CTYPE[s.ty], s.acc, note))
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
        wr('#include <math.h>\n')
        wr('#include <string.h>\n')
        wr('#include <stdlib.h>\n\n')
        wr('/* ---- constants ---- */\n')
        names = list(self.globals.keys())
        names.sort()
        for nm in names:
            s = self.globals[nm]
            if s.is_const:
                wr('#define %s %s\n' % (cvar(nm), s.acc))
        wr('\n/* ---- global variables ---- */\n')
        for ln in self.global_decls():
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
            wr(' * struct layout crosses the bcrun VM boundary ---- */\n')
            wr('static const int __mmb_data_kind[] = {\n')
            for kind, f, i, sv in self.data:
                wr('    %d,\n' % kind)
            wr('};\n')
            wr('static const MMFLOAT __mmb_data_f[] = {\n')
            for kind, f, i, sv in self.data:
                wr('    %s,\n' % f)
            wr('};\n')
            wr('static const MMINTEGER __mmb_data_i[] = {\n')
            for kind, f, i, sv in self.data:
                wr('    %s,\n' % i)
            wr('};\n')
            wr('static const char *__mmb_data_s[] = {\n')
            for kind, f, i, sv in self.data:
                wr('    %s,\n' % sv)
            wr('};\n')
        wr('\n/* ---- forward declarations ---- */\n')
        if self.uses_clear:
            wr('static void __mmb_clear(void);\n')
        rn = list(self.routines.keys())
        rn.sort()
        for nm in rn:
            wr(self.signature(self.routines[nm]) + ';\n')
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
        wr('\n/* ---- main program ---- */\n')
        wr('int main(void)\n{\n')
        wr('    unsigned __mark = mm_mark(); (void)__mark;\n')
        if self.data:
            wr('    mm_data_init4(__mmb_data_kind, __mmb_data_f, '
               '__mmb_data_i, __mmb_data_s, %d);\n' % len(self.data))
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
            s.acc = cvar(nm)


def convert(inpath, outpath=None, report=False, lenient=True, fcc=False):
    lines = []
    f = open(inpath, 'r')
    try:
        while True:
            ln = f.readline()
            if not ln:
                break
            lines.append(ln)
    finally:
        f.close()

    conv = Conv(lines, inpath)
    conv.lenient = lenient
    conv.fcc = fcc
    conv.pass_routine_names()
    conv.pass_declarations()
    conv.walk('scan')
    # constants become #define, so fix their access text before emitting
    consts = {}
    for nm in conv.globals:
        if conv.globals[nm].is_const:
            consts[nm] = conv.globals[nm].acc
    for nm in consts:
        conv.globals[nm].acc = cvar(nm)
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
            print("  line %d: %s" % (ln, why))
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

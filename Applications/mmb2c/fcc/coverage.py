"""Print the MMBasic coverage appendix, read out of the translator.

  python3 fcc/coverage.py            markdown for the PC3 manual
  python3 fcc/coverage.py --check    just the counts

The lists come from mmb2c.py itself - the statement dispatch in
statement_inner() and the BUILTINS table - so the appendix cannot drift
away from what the translator actually does.

DO NOT PASTE THE WHOLE OUTPUT OVER APPENDIX C.  The manual's appendix
opens with the two tables this prints and then continues with pages of
hand-written detail - the MATH sections, error handling, I2C2, SPI,
one-wire, and a "Not covered" that says far more than the one below.
Replacing the appendix wholesale deletes all of it, which is exactly
what happened once.  Take the two TABLES and leave the prose alone.
"""
import re
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "mmb2c.py")
text = open(SRC).read()

# ---- statements: the dispatch chain in statement_inner ---------------
body = text[text.index("def statement_inner"):]
body = body[:body.index("\n    def ", 10)]
stmts = []
# 0-9 in the class, for the same reason the dot matters in the BUILTINS
# pattern below: without it a keyword with a digit never matched its own
# dispatch line, so I2C2 was counted as untranslatable and left out of
# the manual's appendix for as long as it had been translating.  mmedit's
# genkw.py, which reads this file's tables the same way, had it too.
for m in re.finditer(r"up == '([A-Z0-9$?]+)'", body):
    stmts.append(m.group(1))
for m in re.finditer(r"up in \(([^)]*)\)", body):
    for w in re.findall(r"'([A-Z0-9$?]+)'", m.group(1)):
        stmts.append(w)
# TYPE blocks are dispatched through skip_type_block rather than an
# `up ==` test in statement_inner, so the scan above cannot see them
stmts.append('TYPE')
# DefineFont has the same blind spot from the other end: pass_fonts()
# lifts the whole block out BEFORE statement_inner ever runs.  It has
# been translated and board-verified since v0.15 and was missing from
# this appendix - which is the list a reader trusts to say what works.
stmts.append('DEFINEFONT')
stmts = sorted(set(stmts))

# ---- functions: the BUILTINS table -----------------------------------
tbl = text[text.index("BUILTINS = {"):]
tbl = tbl[:tbl.index("\n}")]
funcs = {}
# the dot matters: without it MM.HRES and friends were silently dropped
for name, lo, hi in re.findall(r"'([A-Z0-9.$]+)': \((\d+), (\d+)\)", tbl):
    funcs[name] = (int(lo), int(hi))
# STRUCT(SIZEOF/OFFSET/TYPE) takes a keyword rather than an expression, so
# it is parsed by hand and never reaches BUILTINS - the same blind spot
# TYPE has above
funcs['STRUCT'] = (2, 2)

mathfn = re.search(r"MATHFUNCS = \{([^}]*)\}", text).group(1)
mathfn = re.findall(r"'([A-Z0-9]+)'", mathfn)
# ... and the two families that are HAND branches in expr rather than
# table entries, because their argument shapes do not fit a table:
# BASE64 writes into its second argument, and the CRCs take one to
# seven arguments with any of them omittable.  A table scan cannot see
# either, so this appendix listed five scalar MATH functions while the
# manual's own prose documented BASE64 - asserted against the source so
# a rename here is loud rather than silent, exactly as fcc/mathstatus.py
# registers the same two.
assert "'BASE64'" in text, "the MATH BASE64 branch left mmb2c.py"
assert "CRCWIDTH = {" in text, "the MATH CRC table left mmb2c.py"
mathfn += ["BASE64"]
mathfn += re.findall(r"'(CRC[0-9]+)'",
                     re.search(r"CRCWIDTH = \{([^}]*)\}", text).group(1))
# The vector and matrix functions are hand branches for the same reason
# - they take whole arrays rather than a fixed argument count, and are
# not whole-array REDUCTIONS either, so neither table can hold them.
# Same treatment: asserted against the source so a rename is loud.
assert "'MAGNITUDE', 'DOTPRODUCT'" in text, "the MATH vector branch left mmb2c.py"
assert "'M_DETERMINANT'" in text, "the MATH M_DETERMINANT branch left mmb2c.py"
assert "'CROSSING'" in text, "the MATH CROSSING branch left mmb2c.py"
mathfn += ["MAGNITUDE", "DOTPRODUCT", "M_DETERMINANT", "CROSSING"]
mathfn = sorted(set(mathfn))
matharr = re.search(r"MATHARRAY = \(([^)]*)\)", text).group(1)
matharr = sorted(re.findall(r"'([A-Z0-9]+)'", matharr))

# The IN-PLACE sub-commands, which are statements rather than functions
# and so belong in their own list: what do_array_cmd dispatches, plus
# the component-wise family from the table three lines above it - the
# blind spot that hid all eight of those from COVERAGE-STATUS.md.
blk = text[text.index("def do_array_cmd"):]
blk = blk[:blk.index("\n    def ", 10)]
mathcmd = set(re.findall(r"'([A-Z_0-9]+)'", blk))
assert "CCOMB = {" in text, "the MATH C_* table left mmb2c.py"
ccomb = set(re.findall(r"'([A-Z_0-9]+)'",
                       re.search(r"CCOMB = \{([^}]*)\}", text).group(1)))
mathcmd |= ccomb
# do_array_cmd's block mentions words that are not sub-commands - the
# error text, the type names - so the set above is too generous and has
# to be narrowed to what the function actually DISPATCHES ON.
#
# This used to be a hand-kept list of fourteen names sitting inside the
# generator, which is the one thing a generator must not have: it was
# written before the vector, matrix, quaternion and WINDOW members and
# silently filtered every one of them out, so the manual reported
# fourteen sub-commands while the translator had thirty-two.  The
# dispatch is the authority - `op == 'NAME'` and `op in ('A', 'B')` are
# the only two shapes it uses - so read those and keep no list.
disp = set(re.findall(r"op == '([A-Z_0-9]+)'", blk))
for grp in re.findall(r"op in \(([^)]*)\)", blk):
    disp |= set(re.findall(r"'([A-Z_0-9]+)'", grp))
# ...and the component-wise family, which dispatches as `op in
# self.CCOMB` and so has no quoted name in the function at all.  Its
# eight members come from the table, and the first cut of this filter
# intersected them straight back out again - the same eight the old
# body-scan missed, lost a second time for a different reason.
disp |= ccomb
mathcmd &= disp
mathcmd = sorted(mathcmd)

if "--check" in sys.argv:
    print("statements %d, functions %d, MATH scalar %d, MATH array %d"
          % (len(stmts), len(funcs), len(mathfn), len(matharr)))
    raise SystemExit


def cols(items, width=4):
    """markdown table of names, `width` per row"""
    out = ["| " + " | ".join([" "] * width) + " |",
           "|" + "---|" * width]
    for i in range(0, len(items), width):
        row = items[i:i + width]
        row += [""] * (width - len(row))
        out.append("| " + " | ".join("`%s`" % c if c else "" for c in row)
                   + " |")
    return "\n".join(out)


print("""# Appendix C: MMBasic coverage

This is what `mmbc` translates today. It is generated from the
translator's own tables (`fcc/coverage.py` in the mmb2c repository), so
it says what the program does rather than what anyone remembers it
doing.

Coverage grows with each release, and it will never be complete.
MMBasic is a large language whose statements reach deep into one
particular firmware, and a translator that emits portable C cannot
follow all of it. Anything not listed here is reported by name, with
its line number, and the translation continues - so you find out at
translate time, not at run time.

## Statements
""")
print(cols(stmts))
print("""
Assignment needs no keyword (`LET` is accepted). Statement separators,
line numbers and labels, `REM` and `'` comments all work as expected.

## Functions
""")
print(cols(sorted(funcs)))
print("""
## MATH

Scalar functions, `MATH(name ...)`: %s

Whole-array functions, one number out of an array: %s

In place on arrays, `MATH name ...`: %s

`ARRAY` is accepted as a spelling of `MATH` for `SET`, `ADD`, `SLICE`
and `INSERT`, as it is in MMBasic. It is NOT a spelling of the rest:
`ARRAY SCALE` answers `Unknown command` on a real PicoMite, because
`SCALE` and everything below it live only in `cmd_math`.
""" % (", ".join("`%s`" % f for f in mathfn),
       ", ".join("`%s`" % f for f in matharr),
       ", ".join("`%s`" % f for f in mathcmd)))
print("""## Types and structure

`INTEGER` (64-bit), `FLOAT` (double), `STRING`, and arrays of each, up
to the dimensions MMBasic allows. `DIM`, `LOCAL`, `STATIC`, `CONST`,
`OPTION BASE`, `SUB` and `FUNCTION` with by-reference arguments, and
the usual control flow.

## Not covered

The immediate-mode environment - the editor, `RUN`, `LIST`, `EDIT`,
`NEW`, `SAVE` and the rest - will never apply, because a translated
program is compiled and run rather than typed at a prompt. MMBasic's
in-language PIO assembly is deliberately out too: it gets an assembler
of its own, so that a PIO block can be written once and imported.

What remains are the hardware statements this port has not reached yet
rather than a class it cannot reach: the display, sound, the GPIO
family, `SETPIN`, `PIN`, `PORT`, I2C, SPI, one-wire and the interrupt
sources are all here. `Applications/mmb2c/COVERAGE-STATUS.md` in the
source tree is the honest name-by-name list, generated the same way
this appendix is, and it says which of the rest are wanted, which want
a decision first, and which do not apply to this machine.

Anything not listed anywhere here is refused BY NAME at translate time,
with its line number - so you find out before you run it, not while it
is running.""")

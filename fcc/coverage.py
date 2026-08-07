"""Print the MMBasic coverage appendix, read out of the translator.

  python3 fcc/coverage.py            markdown for the PC3 manual
  python3 fcc/coverage.py --check    just the counts

The lists come from mmb2c.py itself - the statement dispatch in
statement_inner() and the BUILTINS table - so the appendix cannot drift
away from what the translator actually does.  Regenerate it whenever
coverage changes and paste the output into the manual appendix.
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
for m in re.finditer(r"up == '([A-Z$?]+)'", body):
    stmts.append(m.group(1))
for m in re.finditer(r"up in \(([^)]*)\)", body):
    for w in re.findall(r"'([A-Z$?]+)'", m.group(1)):
        stmts.append(w)
# TYPE blocks are dispatched through skip_type_block rather than an
# `up ==` test in statement_inner, so the scan above cannot see them
stmts.append('TYPE')
stmts = sorted(set(stmts))

# ---- functions: the BUILTINS table -----------------------------------
tbl = text[text.index("BUILTINS = {"):]
tbl = tbl[:tbl.index("\n}")]
funcs = {}
for name, lo, hi in re.findall(r"'([A-Z0-9$]+)': \((\d+), (\d+)\)", tbl):
    funcs[name] = (int(lo), int(hi))

mathfn = re.search(r"MATHFUNCS = \{([^}]*)\}", text).group(1)
mathfn = sorted(re.findall(r"'([A-Z0-9]+)'", mathfn))
matharr = re.search(r"MATHARRAY = \(([^)]*)\)", text).group(1)
matharr = sorted(re.findall(r"'([A-Z0-9]+)'", matharr))

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
## MATH() sub-functions

Scalar: %s

Whole-array (one number out of an array): %s
""" % (", ".join("`%s`" % f for f in mathfn),
       ", ".join("`%s`" % f for f in matharr)))
print("""## Types and structure

`INTEGER` (64-bit), `FLOAT` (double), `STRING`, and arrays of each, up
to the dimensions MMBasic allows. `DIM`, `LOCAL`, `STATIC`, `CONST`,
`OPTION BASE`, `SUB` and `FUNCTION` with by-reference arguments, and
the usual control flow.

## Not covered

Everything to do with the firmware's own hardware - display, sound,
GPIO, I2C, SPI, one-wire, interrupts, `SETPIN`, `PIN`, `PORT` - along
with the editor, `RUN`, `LIST`, `EDIT`, `LOAD`, `SAVE` and the rest of
the immediate-mode environment. Some of the hardware statements are the
subject of current work; the immediate-mode ones will never apply, as a
translated program is compiled and run, not typed at a prompt.""")

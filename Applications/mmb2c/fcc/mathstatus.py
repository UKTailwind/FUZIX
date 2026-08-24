"""The MATH family, both halves, with status - read from both sources.

MATH is worth its own section because it is TWO tables inside one
keyword: `MATH <subcommand>` operates on arrays in place, and
`MATH(<subfunction> ...)` returns a value. AllCommands.h has one row
for each, so the main coverage list counts MATH as "translated" and says
nothing at all about the 67 members underneath.
"""
import os
import re

REF = "/mnt/d/Dropbox/PicoMite/PicoMite/core/MATHS.c"
MMB2C = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                     os.pardir, "mmb2c.py")

s = open(REF, encoding="utf-8", errors="replace").read()
i = s.index("void cmd_math")
j = s.index("void fun_math") if "void fun_math" in s else len(s)

ref_cmds = sorted(set(re.findall(
    r'checkstring\(cmdline,\s*\(unsigned char \*\)"([A-Z_0-9]+)"', s[i:j])))
ref_fns = sorted(set(re.findall(
    r'checkstring\((?:ep|pp|tp|p),\s*\(unsigned char \*\)"([A-Z_0-9]+)"',
    s[j:])))

t = open(MMB2C).read()
have_fns = set(re.findall(r"'([A-Z0-9]+)'",
                          re.search(r"MATHFUNCS = \{([^}]*)\}", t).group(1)))
have_fns |= set(re.findall(r"'([A-Z0-9]+)'",
                           re.search(r"MATHARRAY = \(([^)]*)\)", t).group(1)))
# MATH(BASE64 ENCODE/DECODE ...) is a hand branch in expr - its odd
# write-to-the-second-argument shape does not fit the tables - so the
# table scan cannot see it.  Registered here the way mkstatus keeps its
# false-negatives list: asserted against the source so this line goes
# stale LOUDLY if the branch is ever renamed, and the three names are
# the three checkstrings MMBasic's own fun_math makes for the feature.
assert "'BASE64'" in t, "the MATH BASE64 branch left mmb2c.py"
have_fns |= {"BASE64", "ENCODE", "DECODE"}
# MATH(CRC8/12/16/32 ...) is a hand branch for the same reason - one to
# seven arguments, any of them omittable - and is registered from its
# own table so the four names cannot drift apart from what do_math_crc
# actually dispatches.
assert "CRCWIDTH = {" in t, "the MATH CRC table left mmb2c.py"
have_fns |= set(re.findall(
    r"'(CRC[0-9]+)'", re.search(r"CRCWIDTH = \{([^}]*)\}", t).group(1)))
# MATH(MAGNITUDE ...) and MATH(DOTPRODUCT ...) are hand branches too -
# they take whole arrays rather than the fixed argument count MATHFUNCS
# holds, and are not whole-array REDUCTIONS either, so neither table can
# carry them.  Same treatment: asserted against the source so a rename
# is loud.
assert "'MAGNITUDE', 'DOTPRODUCT'" in t, "the MATH vector branch left mmb2c.py"
have_fns |= {"MAGNITUDE", "DOTPRODUCT"}
# the in-place sub-commands do_array_cmd dispatches
blk = t[t.index("def do_array_cmd"):]
blk = blk[:blk.index("\n    def ", 10)]
have_cmds = set(re.findall(r"'([A-Z_0-9]+)'", blk))
# ... and the component-wise family, which do_array_cmd dispatches from
# a table THREE LINES ABOVE ITSELF, so scanning the function body alone
# missed all eight and this section reported C_ADD .. C_XOR as missing
# for as long as they have been shipping (tests/cmath.bas, commit
# bb838cbff).  Read the table itself, asserted the way the BASE64 line
# above is so a rename here is loud rather than silent.
assert "CCOMB = {" in t, "the MATH C_* table left mmb2c.py"
have_cmds |= set(re.findall(
    r"'([A-Z_0-9]+)'", re.search(r"CCOMB = \{([^}]*)\}", t).group(1)))
have_cmds = {c for c in have_cmds if c in ref_cmds}


def table(title, ref, have):
    yes = [n for n in ref if n in have]
    no = [n for n in ref if n not in have]
    out = ["### %s - %d of %d\n" % (title, len(yes), len(ref))]
    out.append("**Translated:** " +
               ("`" + "`, `".join(yes) + "`" if yes else "none") + "\n")
    out.append("**Not translated:** `" + "`, `".join(no) + "`\n")
    return out, len(yes), len(ref)


a, ya, ta = table("`MATH <subcommand>` - in-place, on arrays",
                  ref_cmds, have_cmds)
b, yb, tb = table("`MATH(<subfunction> ...)` - returns a value",
                  ref_fns, have_fns)

print("## The MATH family\n")
print("`MATH` is ONE row in AllCommands.h and two tables underneath it, "
      "so the list above counts it as\ntranslated and says nothing about "
      "the %d members. Read from MMBasic's `core/MATHS.c` and mmb2c's\n"
      "`MATHFUNCS`, `MATHARRAY` and `do_array_cmd`.\n" % (ta + tb))
print("**%d of %d members translated.**\n" % (ya + yb, ta + tb))
print("\n".join(a))
print("\n".join(b))
print("""The split is not arbitrary. What is in is what a BASIC program
actually reaches for - the whole-array reductions, the hyperbolic and
log/atan scalars, the in-place array operations, `BASE64` (which the
Gmail recipe made load-bearing: it is the hand branch in `expr`, with
MMBasic's own write-to-the-second-argument shape), and since
2026-08-24 the eight members that needed no new machinery at all:
`SHIFT`, `POWER`, `V_NORMALISE`, `V_CROSS`, `V_PRINT`, `M_PRINT`,
`MAGNITUDE` and `DOTPRODUCT`. `tests/matha.bas` covers them, blessed
line by line against a real MMBasic.

What is out divides into three kinds:

* **pure arithmetic, no platform dependency, each independently
  testable against the interpreter** - what is left of the matrix and
  vector family (`M_MULT`, `M_INVERSE`, `M_TRANSPOSE`,
  `M_DETERMINANT`, `V_MULT`, `V_ROTATE`), the statistics (`CORREL`,
  `CHI`, `CHI_P`, `CROSSING`) and the quaternions (`Q_*`). These are
  category 2: add on demand rather than as a block, because the 3D and
  graphics demos say which are wanted first. All six of the first
  group walk a 2-D array FLAT, and since the storage-order change our
  flat order is MMBasic's, so each is a transcription rather than a
  rewrite. `array_plane()` already hands out what they need.
* **codecs and transforms** - `FFT`, `AES128`, `WINDOW`, `SINC`,
  `INTERPOLATE`. Bigger pieces, still pure arithmetic. `BASE64` and
  all four `CRC`s have shipped; of the rest `WINDOW` and `INTERPOLATE`
  are small and self-contained, `FFT` is now merely large rather than
  undecided (its complex forms want MMBasic's 2-D storage order, which
  our arrays have had since the storage-order change, and `PHASE` and
  `INVERSE` come with it), and `AES128` has no demand behind it.
* **the odd ones out** - `PID` and `SENSORFUSION` carry state between
  calls and want an IMU this machine does not have, and `RAND` needs
  the Mersenne twister `MATH RANDOMIZE` is supposed to seed (see
  NEXT.md, where that divergence is still open).
""")

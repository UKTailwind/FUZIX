"""The MATH family, both halves, with status - read from both sources.

MATH is worth its own section because it is TWO tables inside one
keyword: `MATH <subcommand>` operates on arrays in place, and
`MATH(<subfunction> ...)` returns a value. AllCommands.h has one row
for each, so the main coverage list counts MATH as "translated" and says
nothing at all about the 67 members underneath.
"""
import re

REF = "/mnt/d/Dropbox/PicoMite/PicoMiteV6.03.00/core/MATHS.c"
MMB2C = "/home/peter/src/mmb2c/mmb2c.py"

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
# the in-place sub-commands do_array_cmd dispatches
blk = t[t.index("def do_array_cmd"):]
blk = blk[:blk.index("\n    def ", 10)]
have_cmds = set(re.findall(r"'([A-Z_0-9]+)'", blk))
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
actually reaches for - the six whole-array reductions, the four hyperbolic
and log/atan scalars, and the four in-place array operations. What is out
divides into three kinds:

* **pure arithmetic, no platform dependency, each independently
  testable against the interpreter** - the matrix and vector family
  (`M_MULT`, `M_INVERSE`, `M_TRANSPOSE`, `M_DETERMINANT`, `V_CROSS`,
  `V_NORMALISE`, `V_MULT`, `V_ROTATE`, `MAGNITUDE`, `DOTPRODUCT`), the
  statistics (`CORREL`, `CHI`, `CHI_P`, `CROSSING`, `PHASE`), the
  quaternions (`Q_*`) and the complex-number set (`C_*`). These are
  category 2: add on demand rather than as a block, because the 3D and
  graphics demos say which are wanted first.
* **codecs and transforms** - `FFT`, `AES128`, `CRC8/12/16/32`,
  `BASE64`, `ENCODE`/`DECODE`, `WINDOW`, `SINC`, `INTERPOLATE`. Bigger
  pieces, still pure arithmetic. `MATH CRC` and `BASE64` are the two
  most asked for.
* **the odd ones out** - `PID` and `SENSORFUSION` carry state between
  calls, `M_PRINT` and `V_PRINT` write to the console, `RAND` overlaps
  `RND`, and `SLICE`/`SHIFT`/`INSERT`/`POWER` are array surgery that
  belongs with `ARRAY SLICE` and `ARRAY INSERT` in category 1.
""")

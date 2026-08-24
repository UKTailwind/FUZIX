"""Diff a translated program's blessed output against the interpreter's.

  set MMPORT=COM14
  python ab.py tests/order.bas tests/order.expected
"""
import re, subprocess, sys, os

here = os.path.dirname(os.path.abspath(__file__))
out = subprocess.run([sys.executable, os.path.join(here, "mmbrun.py"), sys.argv[1]],
                     capture_output=True, text=True)
if out.returncode:
    sys.stderr.write(out.stderr)
    sys.exit(out.returncode)

esc = re.compile(r"\x1b\[[0-9;?]*[a-zA-Z]")
got = [esc.sub("", l).rstrip() for l in out.stdout.replace("\r", "").split("\n")]
got = [l for l in got if l.strip() and l.strip() != ">"]
want = [l.rstrip() for l in open(sys.argv[2]).read().split("\n") if l.strip()]

bad = 0
for i in range(max(len(got), len(want))):
    g = got[i] if i < len(got) else "<missing>"
    w = want[i] if i < len(want) else "<extra>"
    if g != w:
        print("line %d\n  mmb2c : %r\n  MMBasic: %r" % (i + 1, w, g))
        bad += 1
print("%d line(s) compared, %d differ" % (max(len(got), len(want)), bad))
sys.exit(1 if bad else 0)

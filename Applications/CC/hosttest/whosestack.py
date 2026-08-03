"""Which binary was running when the board faulted?

addr2line cannot say: every Fuzix process loads at the same PROGLOAD, so
any address resolves against any binary and several look plausible.  The
stack pointer can.  The ELF loader puts the stack window at
ALIGNUP(PT_DYNAMIC vaddr) .. + stacksize, where stacksize is
PT_GNU_STACK's memsz when it asked for more than the 8K default - so
each binary has its own window and only one contains the dump's sp.

    python3 whosestack.py 0x23440        (sp - progload)
"""
import os
import subprocess
import sys

CC = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_STACK = 8192

sp = int(sys.argv[1], 16)

for b in ('bcrun', 'cc0', 'cc1', 'cc2', 'mmbc', 'ccbc', 'bcdump'):
    path = os.path.join(CC, b)
    if not os.path.exists(path):
        continue
    out = subprocess.check_output(
        ['arm-none-eabi-readelf', '-lW', path]).decode('latin-1')
    dyn = stk = None
    for line in out.splitlines():
        f = line.split()
        if len(f) > 5 and f[0] == 'DYNAMIC' and dyn is None:
            dyn = int(f[2], 16)
        if len(f) > 5 and f[0] == 'GNU_STACK' and stk is None:
            stk = int(f[5], 16)
    if dyn is None:
        continue
    if not stk or stk < DEFAULT_STACK:
        stk = DEFAULT_STACK
    lo = (dyn + 7) & ~7
    hi = lo + stk
    print('%-8s window %06x..%06x  %s'
          % (b, lo, hi, 'CONTAINS SP' if lo <= sp < hi else '-'))

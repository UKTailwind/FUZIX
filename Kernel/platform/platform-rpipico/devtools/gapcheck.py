"""How long is a compile actually SILENT for?

The settle time in demo.py is quiet time after the last byte, not the
duration of the command, so what matters is the longest gap between two
bytes while cc is running - the pause where one pass ends and the next
starts.  Set demo.py's BUILD above that and it can never cut a compile
short; set it below and the next command is typed into the middle of
one.  This measures it rather than guessing.

  python gapcheck.py "cc solar_eclipse.c"
"""
import os
import sys
import time

import serial

PORT = os.environ.get("FZPORT", "COM14")
CMD = sys.argv[1] if len(sys.argv) > 1 else "cc ripple.c"

with serial.Serial(PORT, 115200, timeout=0.1) as ser:
    ser.reset_input_buffer()
    ser.write(CMD.encode() + b"\r")
    ser.flush()

    gaps = []
    total = 0
    last = t0 = time.time()
    # Stop once nothing has come for 8s - comfortably past any real gap.
    while time.time() - last < 8.0:
        n = ser.in_waiting
        if n:
            ser.read(n)
            total += n
            now = time.time()
            gaps.append(now - last)
            last = now
        else:
            time.sleep(0.005)

    gaps = gaps[1:]                     # the first is the echo, not a gap
    print("command : %s" % CMD)
    print("bytes   : %d over %.1f s" % (total, last - t0))
    print("longest silence mid-command: %.2f s" % (max(gaps) if gaps else 0))
    print("top five gaps: %s" %
          ", ".join("%.2f" % g for g in sorted(gaps, reverse=True)[:5]))

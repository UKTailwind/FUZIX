"""Time how long a program on the board actually runs.

  python timerun.py "./bubble.bc"

The demonstration needs the animation to last a known number of seconds,
and a frame count times a measured frame time is a prediction, not a
measurement.  This is the measurement: type the command, then wait for
the shell prompt to come back, and report the wall clock in between.
"""
import os
import sys
import time

import serial

PORT = os.environ.get("FZPORT", "COM14")
CMD = sys.argv[1] if len(sys.argv) > 1 else "./bubble.bc"
LIMIT = float(sys.argv[2]) if len(sys.argv) > 2 else 120.0

with serial.Serial(PORT, 115200, timeout=0.1) as ser:
    ser.reset_input_buffer()
    ser.write(CMD.encode() + b"\r")
    ser.flush()
    t0 = time.time()

    buf = ""
    # The prompt is "# " at the start of a line once the program is done.
    while time.time() - t0 < LIMIT:
        n = ser.in_waiting
        if n:
            buf += ser.read(n).decode("latin-1")
            if buf.count("#") >= 1 and buf.rstrip().endswith("#"):
                break
        else:
            time.sleep(0.01)

    print("command  : %s" % CMD)
    print("ran for  : %.2f s" % (time.time() - t0))
    print("output   : %r" % buf[-200:])

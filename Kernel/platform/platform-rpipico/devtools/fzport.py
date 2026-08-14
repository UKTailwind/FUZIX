"""Open the board's console, waiting for whoever has it to let go.

Windows gives a serial port to one process at a time, and a terminal
left open on the board holds it.  Every tool here then died on
PermissionError before doing anything - so a transfer had to be typed
again after the terminal was closed, and a long one had to be watched
in case it failed at the start.

So: wait instead.  The port coming free is a matter of closing a
window, which happens in seconds, and a tool that waits for it can be
started before the window is closed rather than after.

  FZWAIT   seconds to wait before giving up (default 90; 0 = fail at
           once, which is what a script that must not block wants)
"""
import os
import sys
import time

import serial

BAUD = 115200


def port_name():
    return os.environ.get("FZPORT", "COM11")


def open_port(baud=BAUD, timeout=1, wait=None, port=None):
    """serial.Serial, retried while the port is held by someone else."""
    if port is None:
        port = port_name()
    if wait is None:
        wait = float(os.environ.get("FZWAIT", "90"))
    t0 = time.time()
    said = False
    while True:
        try:
            return serial.Serial(port, baud, timeout=timeout)
        except serial.SerialException as e:
            # Only "someone else has it" is worth waiting for.  A port
            # that does not exist - the CH340 moved, or the board is
            # unplugged - will not appear by itself, and waiting 90
            # seconds to say so helps nobody.
            if "PermissionError" not in repr(e):
                raise
            if time.time() - t0 >= wait:
                raise
            if not said:
                sys.stderr.write(
                    "%s is in use - waiting up to %ds for it "
                    "(close the terminal on it)\n" % (port, int(wait)))
                sys.stderr.flush()
                said = True
            time.sleep(1.0)

"""Slow shell driver for the Fuzix console (COM11).

  python fzsh.py <delay_ms> "cmd" ["cmd" ...]

The CH340 does not always come back on the same number after the board
is re-plugged or the DPDT switch is moved, so set FZPORT to say which.

A command is finished when the board has been quiet for FZQUIET seconds
(0.6 by default).  Raise it for a program that goes silent while it
works - an animation loop prints nothing for seconds at a time, and at
0.6 the reply is declared over and its results are lost.
"""
import os, sys, time, serial

PORT, BAUD = os.environ.get("FZPORT", "COM11"), 115200
QUIET = float(os.environ.get("FZQUIET", "0.6"))


def drain(ser, quiet=QUIET, limit=180.0):
    buf = b""
    last = time.time()
    t0 = time.time()
    while time.time() - t0 < limit:
        n = ser.in_waiting
        if n:
            buf += ser.read(n)
            last = time.time()
        elif time.time() - last > quiet:
            break
        else:
            time.sleep(0.05)
    return buf.decode("latin-1")


def slow(ser, line, delay):
    for ch in line:
        ser.write(ch.encode())
        ser.flush()
        time.sleep(delay)
    ser.write(b"\r")
    ser.flush()


def main():
    delay = float(sys.argv[1]) / 1000.0
    with serial.Serial(PORT, BAUD, timeout=0.2) as ser:
        time.sleep(0.2)
        ser.reset_input_buffer()
        slow(ser, "", delay)
        drain(ser, 0.4, 5.0)
        for c in sys.argv[2:]:
            print("$ " + c)
            slow(ser, c, delay)
            print(drain(ser))


main()

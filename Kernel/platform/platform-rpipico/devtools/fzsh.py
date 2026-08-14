"""Slow shell driver for the Fuzix console (COM11).

  python fzsh.py <delay_ms> "cmd" ["cmd" ...]

The CH340 does not always come back on the same number after the board
is re-plugged or the DPDT switch is moved, so set FZPORT to say which.

A command is finished when THE PROMPT COMES BACK.  That is the actual
signal, and waiting for it costs nothing: a command that takes 2 seconds
returns after 2 seconds.

The quiet timer is only the fallback for something that never returns to
a prompt - a game, or a program waiting for a key.  It used to be the
primary signal, which meant every command paid the timeout after it had
already finished, and anything slow needed FZQUIET raised to cover its
whole run: 90 seconds of sitting still to watch a compile that takes 25.

  FZQUIET   seconds of TOTAL silence that end a reply when no prompt
            ever comes back (30).  It is deliberately generous: a
            command that is working may say nothing for a while - cc is
            silent for several seconds before its first dot - and at 2
            seconds the reply was cut off mid-compile and the rest of
            the dots landed in the next command's transcript.  Lower it
            only for something that stays at a prompt-less screen.
  FZLIMIT   hard ceiling per command (300)
  FZPROMPT  what the prompt looks like (default "# ")
"""
import os, sys, time, serial
import fzport

PORT, BAUD = os.environ.get("FZPORT", "COM11"), 115200
QUIET = float(os.environ.get("FZQUIET", "30"))
LIMIT = float(os.environ.get("FZLIMIT", "300"))
PROMPT = os.environ.get("FZPROMPT", "# ").encode()


def drain(ser, quiet=QUIET, limit=LIMIT, prompt=True):
    buf = b""
    last = time.time()
    t0 = time.time()
    while time.time() - t0 < limit:
        n = ser.in_waiting
        if n:
            buf += ser.read(n)
            last = time.time()
            # The shell back at a prompt is the command finished.  Only
            # at the END of what has arrived: the prompt that preceded
            # the command is in here too, ahead of its echo.
            if prompt and buf.endswith(PROMPT):
                break
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
    with fzport.open_port(BAUD, timeout=0.2) as ser:
        time.sleep(0.2)
        ser.reset_input_buffer()
        slow(ser, "", delay)
        drain(ser, 0.4, 5.0)
        for c in sys.argv[2:]:
            print("$ " + c)
            slow(ser, c, delay)
            print(drain(ser))


main()

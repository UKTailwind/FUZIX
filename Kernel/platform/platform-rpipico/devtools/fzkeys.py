"""Drive a full-screen program on the board and show what it painted.

fzsh.py reads to a shell prompt, which a program that owns the screen
never gives it.  This one takes a command and then a script of key
groups, waiting between them, and prints the output with the escape
sequences made visible - so an editor session can be checked from here
without anyone looking at the monitor.

  python fzkeys.py "mmedit /root/t.bas" "\\x1bOP" "\\x1b"

Keys use Python escapes: \\x1b for ESC, \\r for Return, \\x1bOP for F1.
"""
import os
import sys
import time
import serial

# FZPORT like fzsh/ctrl/uusend: the CH340 does not come back on the same
# number after a re-plug or a switch move, and this was the one helper
# still hardcoded, which made it unusable on the day the board moved.
PORT, BAUD = os.environ.get("FZPORT", "COM11"), 115200


# QUIET GAP, and why it is this long.
# 
# Typing into a console that is still streaming is a known way to lock
# the PC3 up - it is why fzsh.py reads to a prompt and uusend.py
# refuses to start unless the line is idle.  A full screen repaint here
# is several kilobytes AND the editor stops to compute syntax colours
# between lines, so it goes quiet for a long time mid-paint while being
# nowhere near finished.  A short gap reads that pause as "done" and
# sends the next key into the middle of the stream.
# 
# 3 seconds is well past anything the editor pauses for.  If a test
# ever looks like it hung, raise this before suspecting the board.
QUIET = 3.0


def drain(ser, quiet=QUIET, limit=60.0):
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
    return buf


def show(b):
    return (b.decode("latin-1")
            .replace("\033", "<ESC>")
            .replace("\r", "<CR>")
            .replace("\n", "<LF>\n"))


def main():
    cmd = sys.argv[1]
    groups = [g.encode().decode("unicode_escape").encode("latin-1")
              for g in sys.argv[2:]]

    with serial.Serial(PORT, BAUD, timeout=0.2) as ser:
        time.sleep(0.2)
        ser.reset_input_buffer()
        # The sync Return is only to get a fresh shell prompt.  Do NOT
        # send it when attaching to a program already running: a full
        # screen editor takes it as a keystroke, and then every session
        # looks modified.
        if cmd != "-":
            ser.write(b"\r")
            ser.flush()
            drain(ser, 0.4, 5.0)

        # "-" means the program is already running: send only the keys.
        # An empty string will NOT do - a shell can drop it before this
        # sees it, and then the first key group becomes the command and
        # gets typed into the file as literal text.
        if cmd != "-":
            for ch in cmd:
                ser.write(ch.encode())
                ser.flush()
                time.sleep(0.02)
            ser.write(b"\r")
            ser.flush()
            print("$ " + cmd)
            print(show(drain(ser)))

        for g in groups:
            print("--- keys %r ---" % g)
            # One write per GROUP, not per byte: an escape sequence has to
            # arrive as a burst or the editor's inkey() times out between
            # the ESC and the letter and reports a bare Escape - which is
            # what a real terminal does too.  Put each sequence in its own
            # group and ordinary text in another.
            ser.write(g)
            ser.flush()
            print(show(drain(ser)))


main()

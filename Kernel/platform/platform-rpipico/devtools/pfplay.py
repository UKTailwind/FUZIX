"""Drive picofrog from the console and keep everything it says.

The game is played on the board's own keyboard, so a crash that needs a
keypress to provoke needed a person at the machine - and the console
transcript, which is where the fault message goes, was gone by the time
anyone looked.  INKEY$ reads the console too, so the whole sequence can
be driven from here: start it, press space at the title, then move.

  python pfplay.py [program] [seconds]

Everything the board says is printed and written to pfplay.log.
"""
import os
import sys
import time

import fzport

PROG = sys.argv[1] if len(sys.argv) > 1 else "pfns.bc"
SECS = float(sys.argv[2]) if len(sys.argv) > 2 else 40.0

UP, DOWN, LEFT, RIGHT = b"\x1b[A", b"\x1b[B", b"\x1b[D", b"\x1b[C"


def main():
    log = open("pfplay.log", "wb")
    with fzport.open_port(timeout=0.2) as ser:
        time.sleep(0.3)
        ser.reset_input_buffer()

        def say(b, note):
            sys.stdout.write("\n--- %s ---\n" % note)
            sys.stdout.flush()
            log.write(b"\n--- %s ---\n" % note.encode())
            ser.write(b)
            ser.flush()

        def listen(secs):
            t0 = time.time()
            while time.time() - t0 < secs:
                n = ser.in_waiting
                if n:
                    d = ser.read(n)
                    log.write(d)
                    log.flush()
                    sys.stdout.write(d.decode("latin-1"))
                    sys.stdout.flush()
                else:
                    time.sleep(0.05)

        say(b"\r", "wake")
        listen(1.0)
        say(("bcrun %s\r" % PROG).encode(), "run")
        listen(10.0)                    # title screen
        say(b" ", "space - start")
        listen(6.0)
        for k, name in ((UP, "up"), (UP, "up"), (LEFT, "left"),
                        (RIGHT, "right"), (UP, "up")):
            say(k, name)
            listen(4.0)
        listen(SECS)
    log.close()
    sys.stderr.write("\ntranscript in pfplay.log\n")


main()

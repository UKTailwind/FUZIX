"""Fetch a TEXT file off the board, over the console.

uusend.py goes one way only, and everything written on the board - a
program typed in mmedit, a test someone wrote there - was stranded: the
card is the only copy, and re-imaging the card destroys it.

  python fzget.py /root/cc/brownian.bas [local]

Text only, and deliberately: `cat` between two markers needs nothing on
the board that is not already there.  Binaries would need uuencode,
which the board does not have (uusend relies on `uud`, the other
direction).

CR is dropped: the console is a tty and turns every LF into CRLF on the
way out, which would otherwise double up on a file that already has
them.
"""
import os
import sys
import time

import fzport

BEGIN = "__FZGET_BEGIN__"
END = "__FZGET_END__"


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    remote = sys.argv[1]
    local = sys.argv[2] if len(sys.argv) > 2 else os.path.basename(remote)

    with fzport.open_port(timeout=0.5) as ser:
        time.sleep(0.3)
        ser.reset_input_buffer()
        ser.write(("echo %s; cat %s; echo %s\r" % (BEGIN, remote, END))
                  .encode())
        ser.flush()

        buf = b""
        t0 = time.time()
        while time.time() - t0 < 180:
            n = ser.in_waiting
            if n:
                buf += ser.read(n)
                if END.encode() in buf.split(b"\n", 1)[-1] or \
                   buf.count(END.encode()) >= 2:
                    break
            else:
                time.sleep(0.05)

    text = buf.decode("latin-1").replace("\r", "")
    lines = text.split("\n")
    # The command is echoed by the tty, so BEGIN and END each appear
    # twice: once in the echo of the command line, once as output.  The
    # file is between the LAST BEGIN and the END that follows it.
    try:
        b = max(i for i, l in enumerate(lines) if l.strip() == BEGIN)
        e = min(i for i, l in enumerate(lines)
                if l.strip() == END and i > b)
    except ValueError:
        sys.exit("markers not found - is the board at a prompt?\n"
                 + text[-400:])

    body = "\n".join(lines[b + 1:e])
    open(local, "w", newline="\n").write(body + ("\n" if body else ""))
    print("%s -> %s (%d bytes, %d lines)"
          % (remote, local, len(body), len(lines[b + 1:e])))


main()

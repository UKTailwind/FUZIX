"""Run the send/decode/compile/run cycle over and over, sampling the
kernel's block buffer cache between every step.

  python bufwatch.py [cycles] [file.c]

Why: the board dies with "panic: no free buffers", and has done so while
idle - see ../NOTES-buffer-panic.md. Idle means it is not buffer
pressure, it is a leak, and a leak of one buffer per operation is
invisible until the twentieth one. So rather than wait for the panic and
guess, do the suspect work in a loop and read the pool after each step.
Whichever step is holding a buffer when the next sample is taken is the
one that loses them.

`bufs -q` prints a fixed line this parses:

    bufs 20 busy 0 dirty 0 cached 17 clock 3918

On an idle machine every buffer should read busy 0. The sample is taken
by a separate process, so by the time it prints, the previous step's
buffers have been released -- unless they have been leaked. A non-zero
busy count here is therefore the event, not a race, and `bufs` without
-q will name the pid and syscall that pinned it.
"""
import sys, os, time, re, serial

import uusend
from uusend import drain, send, decode

PORT, BAUD = uusend.PORT, uusend.BAUD

WORK = "/root/bw"               # basename used on the board
LINE = re.compile(r"bufs (\d+) busy (\d+) dirty (\d+) cached (\d+) clock (\d+)")


def cmd(ser, c, quiet=1.0, limit=30.0):
    ser.write((c + "\r").encode()); ser.flush()
    time.sleep(0.2)
    return drain(ser, quiet, limit)


def sample(ser):
    """One reading of the buffer pool. Returns (busy, dirty, cached, clock)."""
    out = cmd(ser, "bufs -q", 0.8, 15.0)
    m = LINE.search(out)
    if not m:
        return None
    return tuple(int(x) for x in m.groups()[1:])


def pad_to(src, size):
    """Grow a C source to `size` bytes with a trailing comment.

    Size is the variable that matters. Every sample in the tree is under
    4K, which is inside the 18 direct blocks of an inode, so a run over
    those never touches an indirect block at all - and large transfers
    are what precede the crash. Padding keeps the file compilable while
    pushing it past 9216 bytes (single indirect) and 140288 (double).
    """
    text = open(src, "r").read()
    if size <= len(text):
        return src
    filler = size - len(text) - len("\n/*\n*/\n")
    body = ("x" * 63 + "\n") * (filler // 64)
    body += "x" * (filler % 64)
    out = os.path.join(os.environ.get("TEMP", "/tmp"), "bw_pad.c")
    open(out, "w").write(text + "\n/*\n" + body + "*/\n")
    return out


def main():
    argv = [a for a in sys.argv[1:]]
    size = 0
    if "--pad" in argv:
        i = argv.index("--pad")
        size = int(argv[i + 1])
        del argv[i:i + 2]

    cycles = int(argv[0]) if len(argv) > 0 else 10
    src = argv[1] if len(argv) > 1 else None
    if src is None:
        here = os.path.dirname(os.path.abspath(__file__))
        src = os.path.join(here, "..", "..", "..", "..",
                           "Applications", "CC", "hosttest", "samples", "fmt.c")
    src = os.path.abspath(src)
    if not os.path.exists(src):
        print("no such file: %s" % src)
        return 1
    if size:
        src = pad_to(src, size)

    ser = serial.Serial(PORT, BAUD, timeout=1)
    time.sleep(0.3); ser.reset_input_buffer()
    ser.write(b"\r"); ser.flush(); drain(ser, 0.4, 3)

    print("source %s (%d bytes), %d cycles"
          % (src, os.path.getsize(src), cycles))
    print("%5s %-9s %5s %5s %6s %7s" %
          ("cycle", "step", "busy", "dirty", "cached", "clock"))

    rows = []
    holder = {}

    def note(cycle, step):
        s = sample(ser)
        if s is None:
            print("%5d %-9s  no reading - board not answering" % (cycle, step))
            return False
        busy, dirty, cached, clock = s
        print("%5d %-9s %5d %5d %6d %7d%s" %
              (cycle, step, busy, dirty, cached, clock,
               "   <-- PINNED" if busy else ""))
        rows.append((cycle, step, busy, dirty, cached))
        if busy and not holder:
            holder.update(step=step, cycle=cycle, busy=busy)
            # Name it now, while it is still held: bare `bufs` lists the
            # pinned entries with the pid and syscall that pinned them.
            print(cmd(ser, "bufs", 1.0, 15.0))
        return True

    cmd(ser, "cd /root", 0.5, 10.0)
    if not note(0, "baseline"):
        return 1

    for c in range(1, cycles + 1):
        send(ser, src, WORK + ".c", 0.0, verbose=False)
        if not note(c, "send"):
            break
        decode(ser, WORK + ".c", verbose=False)
        if not note(c, "decode"):
            break
        cmd(ser, "cc %s.c" % WORK, 1.0, 120.0)
        if not note(c, "compile"):
            break
        cmd(ser, "bcrun %s.bc" % WORK, 1.0, 60.0)
        if not note(c, "run"):
            break
        cmd(ser, "rm -f %s.c %s.c.uu %s.bc" % (WORK, WORK, WORK), 0.5, 15.0)
        if not note(c, "cleanup"):
            break

    ser.close()

    print("")
    if holder:
        print("first pinned buffer after step '%s' of cycle %d (%d busy)" %
              (holder["step"], holder["cycle"], holder["busy"]))
    else:
        print("no buffer stayed pinned across %d cycles" % cycles)

    # A leak that is slower than the run still shows as drift in how many
    # buffers are cached and never come free again, so print the trend.
    if rows:
        print("cached: first %d, last %d" % (rows[0][4], rows[-1][4]))
    return 0


if __name__ == "__main__":
    sys.exit(main())

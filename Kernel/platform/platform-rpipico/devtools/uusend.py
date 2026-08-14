"""Send a binary to the PC3 over the console as uuencoded text.

  python uusend.py <localfile> <remotename> [gap_ms]

Avoids XMODEM entirely. rx/sx assume a serial port separate from the
console -- they poll the console for a keypress to cancel, so on a
machine where the console *is* the link the first byte of the transfer
reads as "cancel". Sending printable text through the line discipline
has no such problem, and needs no second UART.

Writes the text with "cat > name.uu", then runs "uud" on the board.
Lines are 62 characters, well inside the 132-byte tty queue, with a
gap after each so cat can drain it.
"""
import sys, time, serial, binascii, os
import fzport

PORT, BAUD = os.environ.get("FZPORT", "COM11"), 115200


def uuencode(data, name):
    out = ["begin 644 %s" % name]
    for i in range(0, len(data), 45):
        chunk = data[i:i + 45]
        line = binascii.b2a_uu(chunk).decode("ascii").rstrip("\n")
        # Traditional backtick-for-space: a trailing space is fragile
        # through a terminal, and decoders mask to 6 bits so ` == 0.
        out.append(line.replace(" ", "`"))
    out.append("`")
    out.append("end")
    return out


"""Everything the board says during a transfer is logged.

Two transfers have killed the machine mid-send and left nothing to look
at, because the echo bytes this waits on were counted and discarded. If
it dies again the tail of this log is the board's last words.
"""
LOGPATH = os.path.join(os.environ.get("TEMP", "/tmp"), "uusend.log")
_log = None
_tail = bytearray()


def logbytes(b):
    global _log
    if _log is None:
        _log = open(LOGPATH, "wb")
    _log.write(b)
    _log.flush()
    _tail.extend(b)
    del _tail[:-4096]


def tail(n):
    return bytes(_tail[-n:]).decode("latin-1", "replace")


def drain(ser, quiet=1.0, limit=15.0):
    buf = b""; last = t0 = time.time()
    while time.time() - t0 < limit:
        n = ser.in_waiting
        if n:
            chunk = ser.read(n); logbytes(chunk)
            buf += chunk; last = time.time()
        elif time.time() - last > quiet:
            break
        else:
            time.sleep(0.02)
    return buf.decode("latin-1", "replace")


def send(ser, local, remote, gap, verbose=True):
    """Type a file into the board over an already-open port.

    Split out from main() so bufwatch.py can drive a transfer as one
    step of a longer cycle without handing the port back and forth.
    """
    def say(*a):
        if verbose:
            print(*a)

    data = open(local, "rb").read()
    lines = uuencode(data, remote)
    say("%s -> %s: %d bytes, %d lines" % (local, remote, len(data), len(lines)))

    # Echo stays ON deliberately: it is the only flow signal available.
    # A fixed delay per line is not enough -- the tty queue is 132 bytes
    # and a 62-byte line sent every 25ms overruns it, losing about 60%
    # of the text. Waiting for each line to echo back keeps at most one
    # line outstanding.
    ser.write(("rm -f %s.uu %s\r" % (remote, remote)).encode()); ser.flush()
    time.sleep(0.4); drain(ser, 0.4, 3)
    ser.write(("cat > %s.uu\r" % remote).encode()); ser.flush()
    time.sleep(0.5); drain(ser, 0.4, 3)

    t0 = time.time()
    for n, line in enumerate(lines, 1):
        payload = line.encode("ascii") + b"\n"
        ser.write(payload); ser.flush()
        # Wait for the echo of this line before sending the next.
        want = len(payload)
        got = 0
        deadline = time.time() + 5.0
        while got < want and time.time() < deadline:
            k = ser.in_waiting
            if k:
                chunk = ser.read(k)
                got += len(chunk)
                logbytes(chunk)
            else:
                time.sleep(0.002)
        if got < want:
            # The board has stopped echoing. Whatever it said last is
            # the only evidence of why, and it used to be counted and
            # thrown away - two transfers died here with nothing kept.
            print("  line %d: echo timeout (%d/%d)" % (n, got, want))
            print("  last from board: %r" % (tail(400),))
            print("  log: %s" % LOGPATH)
        time.sleep(gap)
        if n % 50 == 0:
            say("  %d/%d" % (n, len(lines)))
    ser.write(b"\x04")          # end of input for cat
    ser.flush()
    time.sleep(1.0)
    say("sent in %.1fs" % (time.time() - t0))
    say(drain(ser, 1.0, 8))
    return len(data)


def decode(ser, remote, verbose=True):
    """Run uud on a file just sent. Separate from send() so a caller can
    sample the kernel between typing the text in and decoding it."""
    ser.write(("uud %s.uu\r" % remote).encode()); ser.flush()
    time.sleep(1.0)
    out = drain(ser, 1.5, 20)
    if verbose:
        print(out)
    return out


def main():
    # --port=COMn picks the board (COM11 the PC3, COM14 the PC2); it may
    # appear anywhere, so the positional arguments keep their places.
    argv = [a for a in sys.argv[1:] if not a.startswith("--port=")]
    port = PORT
    for a in sys.argv[1:]:
        if a.startswith("--port="):
            port = a[7:]

    local, remote = argv[0], argv[1]
    gap = (int(argv[2]) if len(argv) > 2 else 25) / 1000.0

    ser = fzport.open_port(BAUD, timeout=1, port=port)
    time.sleep(0.3); ser.reset_input_buffer()
    ser.write(b"\r"); ser.flush(); drain(ser, 0.4, 3)

    send(ser, local, remote, gap)
    decode(ser, remote)
    ser.write(("ls -l %s\r" % remote).encode()); ser.flush()
    time.sleep(0.5)
    print(drain(ser, 1.5, 10))
    return 0


if __name__ == "__main__":
    sys.exit(main())

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

PORT, BAUD = "COM11", 115200


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


def drain(ser, quiet=1.0, limit=15.0):
    buf = b""; last = t0 = time.time()
    while time.time() - t0 < limit:
        n = ser.in_waiting
        if n:
            buf += ser.read(n); last = time.time()
        elif time.time() - last > quiet:
            break
        else:
            time.sleep(0.02)
    return buf.decode("latin-1", "replace")


def main():
    local, remote = sys.argv[1], sys.argv[2]
    gap = (int(sys.argv[3]) if len(sys.argv) > 3 else 25) / 1000.0

    data = open(local, "rb").read()
    lines = uuencode(data, remote)
    print("%s -> %s: %d bytes, %d lines" % (local, remote, len(data), len(lines)))

    ser = serial.Serial(PORT, BAUD, timeout=1)
    time.sleep(0.3); ser.reset_input_buffer()
    ser.write(b"\r"); ser.flush(); drain(ser, 0.4, 3)

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
                got += len(ser.read(k))
            else:
                time.sleep(0.002)
        if got < want:
            print("  line %d: echo timeout (%d/%d)" % (n, got, want))
        time.sleep(gap)
        if n % 50 == 0:
            print("  %d/%d" % (n, len(lines)))
    ser.write(b"\x04")          # end of input for cat
    ser.flush()
    time.sleep(1.0)
    print("sent in %.1fs" % (time.time() - t0))
    print(drain(ser, 1.0, 8))

    ser.write(("uud %s.uu\r" % remote).encode()); ser.flush()
    time.sleep(1.0)
    print(drain(ser, 1.5, 20))
    ser.write(("ls -l %s\r" % remote).encode()); ser.flush()
    time.sleep(0.5)
    print(drain(ser, 1.5, 10))
    return 0


sys.exit(main())

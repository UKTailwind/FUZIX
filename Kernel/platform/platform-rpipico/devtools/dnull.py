"""Console-flood discriminator: uusend's exact stream into /dev/null.

Same uuencoded text, same one-line-outstanding echo pacing, same tty
path - but cat writes to /dev/null, so the filesystem never sees it.
If this wedges the console, the fault is in the uart/echo path; if it
survives while a real transfer dies, the fault is in the fs write path.

  python dnull.py <localfile> [gap_ms]

Exit code 0 = stream completed and the shell answered afterwards.
"""
import os, sys, time, serial
from uusend import uuencode, logbytes, tail, drain, LOGPATH

PORT, BAUD = os.environ.get("FZPORT", "COM11"), 115200


def main():
    local = sys.argv[1]
    gap = (int(sys.argv[2]) if len(sys.argv) > 2 else 0) / 1000.0
    data = open(local, "rb").read()
    lines = uuencode(data, "dnull")
    print("%s -> /dev/null: %d bytes, %d lines" % (local, len(data), len(lines)))

    with serial.Serial(PORT, BAUD, timeout=0.3) as ser:
        time.sleep(0.3)
        ser.reset_input_buffer()
        ser.write(b"\r"); ser.flush()
        drain(ser, 0.4, 3)
        ser.write(b"cat > /dev/null\r"); ser.flush()
        time.sleep(0.5); drain(ser, 0.4, 3)

        t0 = time.time()
        for n, line in enumerate(lines, 1):
            payload = line.encode("ascii") + b"\n"
            ser.write(payload); ser.flush()
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
                print("  WEDGED at line %d/%d (echo %d/%d)" %
                      (n, len(lines), got, want))
                print("  log: %s" % LOGPATH)
                sys.exit(1)
            if gap:
                time.sleep(gap)
            if n % 200 == 0:
                print("  %d/%d" % (n, len(lines)))
        ser.write(b"\x04"); ser.flush()
        time.sleep(0.5); drain(ser, 0.5, 3)
        ser.write(b"echo TRANSFER-OK\r"); ser.flush()
        time.sleep(0.3)
        reply = drain(ser, 0.6, 5)
        ok = "TRANSFER-OK" in reply
        print("completed in %.1fs, shell %s" %
              (time.time() - t0, "alive" if ok else "NOT RESPONDING"))
        sys.exit(0 if ok else 2)


main()

"""Run a bytecode program on the PC3 and diff its output against the
gcc reference captured on the host.

  python hwdiff.py optest.bc optest.ref.out
"""
import serial, time, sys, difflib

PORT, BAUD = "COM11", 115200
prog, ref = sys.argv[1], sys.argv[2]

ser = serial.Serial(PORT, BAUD, timeout=0.5)
time.sleep(0.4)
ser.reset_input_buffer()


def send(s, d=0.02):
    for ch in s:
        ser.write(ch.encode()); ser.flush(); time.sleep(d)
    ser.write(b"\r"); ser.flush()


def drain(limit=60, quiet=3.0):
    buf = b""; last = t0 = time.time()
    while time.time() - t0 < limit:
        n = ser.in_waiting
        if n:
            buf += ser.read(n); last = time.time()
        elif time.time() - last > quiet:
            break
        else:
            time.sleep(0.03)
    return buf.decode("latin-1", "replace")


send("")
drain(4, 1.0)
send("./bcrun " + prog)
out = drain(60, 3.0)

# Strip the echoed command and the trailing shell prompt
lines = [l.rstrip("\r") for l in out.split("\n")]
lines = [l for l in lines if l.strip() not in ("", "#") and
         not l.startswith("./bcrun")]

want = [l.rstrip("\n") for l in open(ref).read().split("\n") if l.strip()]

if lines == want:
    print("PASS  hardware output identical to the gcc reference (%d lines)"
          % len(want))
    sys.exit(0)

print("FAIL  hardware output differs from the gcc reference:")
for d in difflib.unified_diff(want, lines, "gcc", "pc3", lineterm=""):
    print("  " + d)
sys.exit(1)

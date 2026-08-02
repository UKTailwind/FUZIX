"""Passive serial capture: everything the board says for N seconds.

  python listen.py 30 out.txt [--port=COMn]

--port picks the board: COM11 is the PC3, COM14 the PC2.  It defaults to
COM11, which silently captures nothing when the board you are watching is
the other one.
"""
import sys, time, serial

secs = float(sys.argv[1])
out = sys.argv[2]
port = "COM11"
for a in sys.argv[3:]:
    if a.startswith("--port="):
        port = a[7:]

with serial.Serial(port, 115200, timeout=0.2) as ser, \
     open(out, "wb") as f:
    t0 = time.time()
    n = 0
    while time.time() - t0 < secs:
        d = ser.read(4096)
        if d:
            f.write(d)
            f.flush()
            n += len(d)
    print("captured %d bytes from %s" % (n, port))

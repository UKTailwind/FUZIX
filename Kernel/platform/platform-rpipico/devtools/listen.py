"""Passive serial capture: everything COM11 says for N seconds.

  python listen.py 30 out.txt
"""
import sys, time, serial

secs = float(sys.argv[1])
out = sys.argv[2]

with serial.Serial("COM11", 115200, timeout=0.2) as ser, \
     open(out, "wb") as f:
    t0 = time.time()
    n = 0
    while time.time() - t0 < secs:
        d = ser.read(4096)
        if d:
            f.write(d)
            f.flush()
            n += len(d)
    print("captured %d bytes" % n)

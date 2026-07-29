"""Send raw control characters to the PC3 console and show what comes back.

  python ctrl.py 03 04 0d
"""
import sys, time, serial

PORT, BAUD = "COM11", 115200

with serial.Serial(PORT, BAUD, timeout=0.3) as ser:
    time.sleep(0.3)
    ser.reset_input_buffer()
    for h in sys.argv[1:]:
        ser.write(bytes([int(h, 16)]))
        ser.flush()
        time.sleep(0.6)
    buf = b""
    t0 = last = time.time()
    while time.time() - t0 < 8:
        n = ser.in_waiting
        if n:
            buf += ser.read(n)
            last = time.time()
        elif time.time() - last > 1.2:
            break
        else:
            time.sleep(0.05)
    print(repr(buf.decode("latin-1")))

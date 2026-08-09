"""Send Ctrl-C to the board and show what came back."""
import os, sys, time, serial

PORT = os.environ.get("FZPORT", "COM14")
with serial.Serial(PORT, 115200, timeout=0.1) as ser:
    ser.write(b"\x03")
    ser.flush()
    time.sleep(1.5)
    ser.write(b"\r")
    ser.flush()
    time.sleep(1.5)
    n = ser.in_waiting
    print(ser.read(n).decode("latin-1") if n else "(nothing)")

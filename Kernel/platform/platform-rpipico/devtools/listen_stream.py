"""Passive serial monitor: print every line COM11 emits, forever.
Survives the port vanishing and re-appearing (board reset/re-enumerate).
"""
import sys, time, serial

buf = b""
while True:
    try:
        with serial.Serial("COM11", 115200, timeout=0.5) as ser:
            print("[listener attached]", flush=True)
            while True:
                d = ser.read(1024)
                if d:
                    buf += d
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        print(line.decode("latin-1", "replace").rstrip("\r"),
                              flush=True)
    except serial.SerialException:
        print("[port lost - retrying]", flush=True)
        time.sleep(1)

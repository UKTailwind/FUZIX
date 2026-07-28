"""Slow, character-at-a-time driver for the Fuzix console (COM11).

  python fz2.py <delay_ms> "LINE" ["LINE" ...]

Sends each character with a delay, so the board's tty queue can never
overflow.  Starts bbcbasic first, QUITs at the end.
"""
import sys, time, serial

PORT, BAUD = "COM11", 115200


def drain(ser, quiet=0.5, limit=15.0):
    buf = b""
    last = time.time()
    t0 = time.time()
    while time.time() - t0 < limit:
        n = ser.in_waiting
        if n:
            buf += ser.read(n)
            last = time.time()
        elif time.time() - last > quiet:
            break
        else:
            time.sleep(0.05)
    return buf.decode("latin-1")


def slow_send(ser, line, delay):
    for ch in line:
        ser.write(ch.encode())
        ser.flush()
        time.sleep(delay)
    ser.write(b"\r")
    ser.flush()


def main():
    delay = float(sys.argv[1]) / 1000.0
    lines = sys.argv[2:]
    with serial.Serial(PORT, BAUD, timeout=0.2) as ser:
        time.sleep(0.2)
        ser.reset_input_buffer()
        slow_send(ser, "", delay)
        drain(ser, 0.4, 3.0)
        slow_send(ser, "bbcbasic", delay)
        print(drain(ser, 0.8, 15.0))
        for ln in lines:
            slow_send(ser, ln, delay)
            print(drain(ser, 0.6, 20.0))
        slow_send(ser, "QUIT", delay)
        drain(ser, 0.8, 10.0)


main()

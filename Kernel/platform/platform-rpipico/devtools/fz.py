"""Drive the Fuzix console on COM11.

  python fz.py probe                 - show what the board is doing now
  python fz.py sh "cmd" ["cmd" ...]  - run shell commands, print output
  python fz.py bas "LINE" ["LINE"..] - run BBC BASIC lines (auto QUIT)
"""
import sys, time, serial

PORT, BAUD = "COM11", 115200


def drain(ser, quiet=0.4, limit=8.0):
    """read until the board goes quiet for `quiet` seconds"""
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


def send(ser, line, quiet=0.4, limit=8.0):
    ser.write(line.encode() + b"\r")
    ser.flush()
    return drain(ser, quiet, limit)


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "probe"
    args = sys.argv[2:]
    with serial.Serial(PORT, BAUD, timeout=0.2) as ser:
        time.sleep(0.2)
        ser.reset_input_buffer()

        if mode == "probe":
            print("--- idle output ---")
            print(drain(ser, 0.5, 2.0))
            print("--- after CR ---")
            print(send(ser, ""))
            return

        if mode == "sh":
            send(ser, "")                    # settle at a prompt
            for c in args:
                print("$ " + c)
                print(send(ser, c, 0.5, 20.0))
            return

        if mode == "bas":
            send(ser, "")
            print(send(ser, "bbcbasic", 0.8, 20.0))
            for line in args:
                print("> " + line)
                print(send(ser, line, 0.6, 20.0))
            print(send(ser, "QUIT", 0.8, 10.0))
            return


main()

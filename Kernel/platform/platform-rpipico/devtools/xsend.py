"""XMODEM sender for the PC3 console.

  python xsend.py <localfile> <remotepath> [chunk] [gap_ms]

Starts "rx <remotepath>" on the board, waits for the receiver's NAK or
'C', then sends 128-byte blocks. Supports both the checksum and CRC-16
flavours depending on what the receiver asks for.

A frame is 132 bytes and the Fuzix tty queue is TTYSIZ = 132, so there
is no headroom at all if the receiver is not draining continuously.
chunk/gap let the frame be dribbled out in pieces; chunk=0 sends the
frame in one write.
"""
import sys, time, serial

PORT, BAUD = "COM11", 115200
SOH, EOT, ACK, NAK, CAN = 0x01, 0x04, 0x06, 0x15, 0x18


def crc16(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def slow(ser, s, d=0.025):
    for ch in s:
        ser.write(ch.encode()); ser.flush(); time.sleep(d)
    ser.write(b"\r"); ser.flush()


def drain(ser, quiet=1.0, limit=8.0):
    buf = b""; last = t0 = time.time()
    while time.time() - t0 < limit:
        n = ser.in_waiting
        if n:
            buf += ser.read(n); last = time.time()
        elif time.time() - last > quiet:
            break
        else:
            time.sleep(0.02)
    return buf


def main():
    local, remote = sys.argv[1], sys.argv[2]
    chunk = int(sys.argv[3]) if len(sys.argv) > 3 else 32
    gap = (int(sys.argv[4]) if len(sys.argv) > 4 else 10) / 1000.0

    data = open(local, "rb").read()
    blocks = [data[i:i + 128].ljust(128, b"\x1a") for i in range(0, len(data), 128)]
    print(f"{local} -> {remote}: {len(data)} bytes, {len(blocks)} blocks, "
          f"chunk={chunk} gap={gap*1000:.0f}ms")

    ser = serial.Serial(PORT, BAUD, timeout=1)
    time.sleep(0.3); ser.reset_input_buffer()
    slow(ser, "")
    drain(ser, 0.4, 3)
    slow(ser, f"rx {remote}")

    # Let the command echo clear first. Without this the echoed text is
    # mistaken for the receiver's handshake -- a remote path containing
    # a capital C reads as "CRC mode".
    echo = drain(ser, 0.5, 3)
    print("echo: " + repr(echo.decode("latin-1", "replace"))[:140])

    # The handshake usually arrives inside that echo, so look there
    # first -- rx only offers it a few times before giving up.
    mode = None
    if NAK in echo:
        mode = "checksum"
    elif echo.rstrip().endswith(b"C"):
        mode = "crc"

    t0 = time.time()
    while mode is None and time.time() - t0 < 20:
        b = ser.read(1)
        if not b:
            continue
        if b[0] == NAK:
            mode = "checksum"; break
        if b == b"C":
            mode = "crc"; break
    if mode is None:
        print("receiver never sent NAK or C -- is rx running?")
        print(repr(drain(ser, 0.5, 3)))
        return 1
    print(f"receiver ready ({mode})")

    sent = 0
    for n, blk in enumerate(blocks, start=1):
        if mode == "crc":
            c = crc16(blk)
            frame = bytes([SOH, n & 0xFF, 255 - (n & 0xFF)]) + blk + bytes([c >> 8, c & 0xFF])
        else:
            frame = bytes([SOH, n & 0xFF, 255 - (n & 0xFF)]) + blk + bytes([sum(blk) & 0xFF])

        for attempt in range(8):
            if chunk:
                for i in range(0, len(frame), chunk):
                    ser.write(frame[i:i + chunk]); ser.flush()
                    time.sleep(gap)
            else:
                ser.write(frame); ser.flush()
            r = ser.read(1)
            if r and r[0] == ACK:
                sent += 1
                break
            if r and r[0] == CAN:
                print(f"\nreceiver cancelled at block {n}")
                return 1
            print(f"  block {n} retry {attempt+1} (got {r!r})")
        else:
            print(f"\nblock {n} failed after 8 tries")
            return 1
        if n % 20 == 0:
            print(f"  {n}/{len(blocks)}")

    ser.write(bytes([EOT])); ser.flush()
    r = ser.read(1)
    print(f"sent {sent} blocks, EOT -> {r!r}")
    print(drain(ser, 1.0, 6).decode("latin-1", "replace"))
    return 0


sys.exit(main())

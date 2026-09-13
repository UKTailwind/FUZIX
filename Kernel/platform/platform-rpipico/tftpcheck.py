"""The client half of tftpcheck.sh.  Run it through that, not directly.

A TFTP client written out rather than borrowed, because the cases that
matter need a client that will misbehave on purpose: drop a packet,
acknowledge a block it already acknowledged, send a filename curl would
have normalised away.
"""
import os
import socket
import struct
import subprocess
import sys
import tempfile
import shutil
import time

PORT = 8069
HOST = "127.0.0.1"
RRQ, WRQ, DATA, ACK, ERROR = 1, 2, 3, 4, 5

fails = []


def ok(name, cond, detail=""):
    print(("  PASS  " if cond else "  FAIL  ") + name +
          ("" if cond else "   " + detail.strip()))
    if not cond:
        fails.append(name)


def req(op, name, mode=b"octet"):
    return struct.pack("!H", op) + name.encode() + b"\0" + mode + b"\0"


def parse(p):
    return struct.unpack("!H", p[:2])[0], struct.unpack("!H", p[2:4])[0], p[4:]


def sock(timeout=3.0):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(timeout)
    return s


def get(name, drop_first_data=False, stale_ack=False, timeout=3.0):
    """RRQ.  Returns (bytes, errcode or None, errmsg)."""
    s = sock(timeout)
    s.sendto(req(RRQ, name), (HOST, PORT))
    body = b""
    want = 1
    dropped = False
    try:
        while True:
            p, _ = s.recvfrom(2048)
            op, blk, payload = parse(p)
            if op == ERROR:
                return None, blk, payload.rstrip(b"\0").decode()
            if op != DATA:
                return None, -1, "opcode %d" % op
            if blk == want and drop_first_data and not dropped:
                dropped = True              # pretend it never arrived
                if stale_ack:
                    s.sendto(struct.pack("!HH", ACK, want - 1), (HOST, PORT))
                continue
            if blk != want:
                s.sendto(struct.pack("!HH", ACK, blk), (HOST, PORT))
                continue
            body += payload
            s.sendto(struct.pack("!HH", ACK, blk), (HOST, PORT))
            if len(payload) < 512:
                return body, None, ""
            want = (want + 1) & 0xFFFF
    finally:
        s.close()


def put(name, body, dup_block=None):
    """WRQ.  dup_block: send that block twice, to test the re-ACK."""
    s = sock()
    s.sendto(req(WRQ, name), (HOST, PORT))
    op, blk, payload = parse(s.recvfrom(2048)[0])
    if op == ERROR:
        s.close()
        return blk, payload.rstrip(b"\0").decode()
    if (op, blk) != (ACK, 0):
        s.close()
        return -1, "opcode %d blk %d" % (op, blk)
    n, off = 1, 0
    while True:
        chunk = body[off:off + 512]
        s.sendto(struct.pack("!HH", DATA, n) + chunk, (HOST, PORT))
        op, blk, payload = parse(s.recvfrom(2048)[0])
        if op == ERROR:
            s.close()
            return blk, payload.rstrip(b"\0").decode()
        if (op, blk) != (ACK, n):
            s.close()
            return -1, "ack %d for block %d" % (blk, n)
        if dup_block == n:
            # Treat that ACK as lost and send the block again.
            s.sendto(struct.pack("!HH", DATA, n) + chunk, (HOST, PORT))
            op, blk, _ = parse(s.recvfrom(2048)[0])
            if (op, blk) != (ACK, n):
                s.close()
                return -1, "no re-ack of block %d (got %d/%d)" % (n, op, blk)
        if len(chunk) < 512:
            s.close()
            return None, ""
        off += 512
        n = (n + 1) & 0xFFFF


def main():
    binary = sys.argv[1]
    d = tempfile.mkdtemp()
    small = os.urandom(1500)
    exact = os.urandom(1024)                # an exact multiple of 512
    big = os.urandom(40000)
    for n, b in (("small.bin", small), ("exact.bin", exact),
                 ("empty.bin", b""), ("big.bin", big)):
        open(os.path.join(d, n), "wb").write(b)

    srv = subprocess.Popen([binary, "-p", str(PORT), d],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    time.sleep(0.4)
    try:
        b, e, m = get("small.bin")
        ok("read 1500 bytes", b == small, "%r %s" % (e, m))

        b, e, m = get("exact.bin")
        ok("read an exact multiple of 512 - the final empty block",
           b == exact, "%r %s" % (e, m))

        b, e, m = get("empty.bin")
        ok("read an empty file", b == b"", "%r %s" % (e, m))

        b, e, m = get("big.bin")
        ok("read 40000 bytes (79 blocks)", b == big, "%r %s" % (e, m))

        b, e, m = get("nosuch.bin")
        ok("a missing file is error 1", e == 1, "%r %s" % (e, m))

        b, e, m = get("../etc/passwd")
        ok("a path with .. is refused", e == 2, "%r %s" % (e, m))

        b, e, m = get("/small.bin")
        ok("a leading / is stripped, not followed", b == small,
           "%r %s" % (e, m))

        e, m = put("up.bin", small)
        got = open(os.path.join(d, "up.bin"), "rb").read() if e is None else None
        ok("write 1500 bytes", e is None and got == small, "%r %s" % (e, m))

        e, m = put("up2.bin", exact)
        got = open(os.path.join(d, "up2.bin"), "rb").read() if e is None else None
        ok("write an exact multiple of 512", e is None and got == exact,
           "%r %s" % (e, m))

        e, m = put("up3.bin", big, dup_block=3)
        got = open(os.path.join(d, "up3.bin"), "rb").read() if e is None else None
        ok("a duplicate DATA is acknowledged again, not errored",
           e is None and got == big, "%r %s" % (e, m))

        e, m = put("../up4.bin", small)
        ok("a write with .. is refused", e == 2, "%r %s" % (e, m))

        # A second client mid-transfer is told so, and the transfer it
        # interrupted still finishes.
        s = sock()
        s.sendto(req(RRQ, "big.bin"), (HOST, PORT))
        op, blk, payload = parse(s.recvfrom(2048)[0])
        other = sock()
        other.sendto(req(RRQ, "small.bin"), (HOST, PORT))
        sop, scode, smsg = parse(other.recvfrom(2048)[0])
        other.close()
        ok("a second client mid-transfer is refused",
           sop == ERROR and scode == 2, "%d/%d %r" % (sop, scode, smsg))
        body, want = payload, 2
        s.sendto(struct.pack("!HH", ACK, 1), (HOST, PORT))
        while True:
            op, blk, payload = parse(s.recvfrom(2048)[0])
            if op != DATA or blk != want:
                break
            body += payload
            s.sendto(struct.pack("!HH", ACK, blk), (HOST, PORT))
            if len(payload) < 512:
                break
            want += 1
        s.close()
        ok("the interrupted transfer still completed", body == big)

        # THE ONE THAT MATTERS.  Drop DATA 1 and answer with a stale ACK
        # 0.  lwIP's server sends ERROR "Wrong block number" here and
        # the transfer dies at the first lost packet; this one must
        # ignore it and let the ten second timer resend.
        t0 = time.time()
        b, e, m = get("small.bin", drop_first_data=True, stale_ack=True,
                      timeout=25.0)
        dt = time.time() - t0
        ok("a stale ACK does not kill the transfer; the timer retransmits",
           b == small, "%r %s after %.1fs" % (e, m, dt))
        ok("...and it waited the ten seconds, not less", 9.0 <= dt <= 13.0,
           "%.1fs" % dt)

        srv.terminate()
        srv.wait()
        srv = subprocess.Popen([binary, "-r", "-p", str(PORT), d],
                               stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
        time.sleep(0.4)
        e, m = put("ro.bin", small)
        ok("-r refuses a write",
           e == 2 and not os.path.exists(os.path.join(d, "ro.bin")),
           "%r %s" % (e, m))
        b, e, m = get("small.bin")
        ok("-r still allows a read", b == small, "%r %s" % (e, m))
    finally:
        srv.terminate()
        srv.wait()
        shutil.rmtree(d, ignore_errors=True)

    if fails:
        print("tftpcheck: FAILED - " + ", ".join(fails))
        sys.exit(1)
    print("tftpcheck: all cases passed")


main()

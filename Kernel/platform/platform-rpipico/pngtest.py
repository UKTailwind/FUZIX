"""pngtest.py - make the PNGs that hit upng's inflate edges, and check them.

  python3 pngtest.py <pngtest binary> <work dir>

Three 8-bit indexed images (indexed because loadpng refuses grey; the
pixel bytes are palette indices, so the deflate stream is unaffected):

  len15.png    LEGAL, and its deflate stream uses 15-bit codes - proven
               below by parsing the block header, not assumed.  It must
               decode exactly as plain.png does.
  plain.png    the same pixels compressed normally (no code over 6 bits).
  backref.png  a hand-made fixed-Huffman stream: one literal, then a
               match at distance 2 - before the start of the data.  It
               must be refused.

How a 15-bit code is forced: a Huffman tree is one straight chain - as
deep as it can be - when every weight beats the sum of all but the two
before it, so weight k = (sum of weights 0..k-2) + 1, which is the
Fibonacci numbers.  19 of them fit one deflate block (zlib cuts a block
at 32767 symbols) and want depth 18, so zlib must clamp codes to 15.
Two things undo it, and both bit while this was written: the end code
occurs once whatever we do, and as an EXTRA weight of 1 it ties with
the chain's own 1s and the tree grows two chains side by side (depth
11) - so it IS weight 0; and every row adds a filter byte, so the
filter bytes are one of the weights too (the row count).
"""
import os
import struct
import subprocess
import sys
import zlib


def chunk(kind, data):
    return (struct.pack(">I", len(data)) + kind + data
            + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))


def png(w, h, zdata):
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0)
    plte = b"".join(bytes(((i * 13) & 255, (i * 97) & 255, (255 - i * 11) & 255))
                    for i in range(19))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"PLTE", plte)
            + chunk(b"IDAT", zdata) + chunk(b"IEND", b""))


class BitReader:
    def __init__(self, data):
        self.d, self.p = data, 0

    def get(self, n):
        v = 0
        for i in range(n):
            v |= ((self.d[self.p >> 3] >> (self.p & 7)) & 1) << i
            self.p += 1
        return v


def canon(lengths):
    """(length, code) -> symbol for a canonical Huffman code."""
    count = [0] * 16
    for l in lengths:
        count[l] += 1
    count[0] = 0
    code, nxt = 0, [0] * 16
    for b in range(1, 16):
        code = (code + count[b - 1]) << 1
        nxt[b] = code
    table = {}
    for sym, l in enumerate(lengths):
        if l:
            table[(l, nxt[l])] = sym
            nxt[l] += 1
    return table


def decode(r, table):
    code = length = 0
    while True:
        code = (code << 1) | r.get(1)
        length += 1
        if (length, code) in table:
            return table[(length, code)]
        if length > 15:
            raise ValueError("bad code")


def longest_code(zdata):
    """The longest literal/length code in the first (dynamic) block."""
    r = BitReader(zdata[2:])
    r.get(1)
    if r.get(2) != 2:
        return None
    hlit, hdist, hclen = r.get(5) + 257, r.get(5) + 1, r.get(4) + 4
    order = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15]
    cl = [0] * 19
    for i in range(hclen):
        cl[order[i]] = r.get(3)
    t = canon(cl)
    lens = []
    while len(lens) < hlit + hdist:
        s = decode(r, t)
        if s < 16:
            lens.append(s)
        elif s == 16:
            lens += [lens[-1]] * (3 + r.get(2))
        elif s == 17:
            lens += [0] * (3 + r.get(3))
        else:
            lens += [0] * (11 + r.get(7))
    return max(lens[:hlit])


class BitWriter:
    def __init__(self):
        self.bits = []

    def put(self, v, n):            # deflate's fields go LSB first
        self.bits += [(v >> i) & 1 for i in range(n)]

    def huff(self, code, n):        # Huffman codes MSB first
        self.bits += [(code >> (n - 1 - i)) & 1 for i in range(n)]

    def data(self):
        out = bytearray((len(self.bits) + 7) // 8)
        for i, b in enumerate(self.bits):
            out[i >> 3] |= b << (i & 7)
        return bytes(out)


def make(out):
    wts = [1, 1]
    while len(wts) < 19:
        wts.append(sum(wts[:-1]) + 1)
    j = next(i for i, v in enumerate(wts) if v >= 64)    # rows = weight j
    h = wts[j]
    rest = [v for i, v in enumerate(wts) if i not in (0, j)]
    rest[-1] += (-sum(rest)) % h         # the top weight absorbs the slack
    w = sum(rest) // h
    pix = bytearray()
    for k, f in enumerate(rest):
        pix += bytes([k + 1]) * f
    raw = b"".join(b"\x00" + bytes(pix[y * w:(y + 1) * w]) for y in range(h))

    c = zlib.compressobj(9, zlib.DEFLATED, 15, 9, zlib.Z_HUFFMAN_ONLY)
    z15 = c.compress(raw) + c.flush()
    if longest_code(z15) != 15 or zlib.decompress(z15) != raw:
        print("FAIL  len15.png has no 15-bit code: the test would prove nothing")
        return False
    open(os.path.join(out, "len15.png"), "wb").write(png(w, h, z15))
    open(os.path.join(out, "plain.png"), "wb").write(png(w, h, zlib.compress(raw, 9)))

    # 4x4: literal 0 (a filter byte), length 3 (code 257) at distance 2
    # (distance code 1), end of block
    bw = BitWriter()
    bw.put(1, 1)                    # BFINAL
    bw.put(1, 2)                    # fixed Huffman
    bw.huff(0x30, 8)
    bw.huff(1, 7)
    bw.huff(1, 5)
    bw.huff(0, 7)
    zb = b"\x78\x01" + bw.data() + struct.pack(">I", zlib.adler32(bytes(20)))
    open(os.path.join(out, "backref.png"), "wb").write(png(4, 4, zb))
    return True


def run(binary, path):
    p = subprocess.run([binary, path], capture_output=True, text=True)
    return p.returncode, (p.stdout + p.stderr).strip()


def main():
    binary, out = sys.argv[1], sys.argv[2]
    if not make(out):
        return 1
    fails = 0

    def check(what, ok, detail):
        nonlocal fails
        print("%s  %s" % ("pass" if ok else "FAIL", what))
        if not ok:
            print("      " + detail.replace("\n", "\n      ")[:2000])
            fails += 1

    res = {n: run(binary, os.path.join(out, n + ".png"))
           for n in ("plain", "len15", "backref")}
    for n, (rc, text) in res.items():
        check("%s.png: no out-of-bounds access" % n, rc != 99, text)
    check("plain.png decodes", res["plain"][0] == 0, res["plain"][1])
    check("len15.png (15-bit codes) decodes", res["len15"][0] == 0, res["len15"][1])
    check("len15.png decodes to exactly plain.png's pixels",
          res["len15"][1].split("fnv")[-1] == res["plain"][1].split("fnv")[-1],
          res["len15"][1] + "\n" + res["plain"][1])
    check("backref.png (a match before the data) is refused",
          res["backref"][0] == 1, res["backref"][1])
    print("pngtest: %s" % ("FAILED" if fails else "all passed"))
    return 1 if fails else 0


sys.exit(main())

"""The checker behind bmptest.sh: loadimage -s against an independent
implementation of the reference's own conversion.

   python3 bmptest.py <path to a host build of loadimage>

MMBasic's SPRITE LOADBMP (graphics/Sprite.c) turns each pixel into a
4-bit index by BIT EXTRACTION - red's top bit, green's top two, blue's
top bit - and NOT with the dithering quantiser its screen path uses.
That one line is the whole of what a sprite loader has to get right,
and it is written out again here rather than shared, so that a change
to loadimage.c has something to disagree with.

The pictures are built here too, so the gate needs no fixtures and no
board: sprite mode never opens /dev/sys, which is what makes a host run
possible at all.
"""
import struct
import subprocess
import sys

LOADIMAGE = sys.argv[1] if len(sys.argv) > 1 else "./loadimage-host"


def bmp24(w, h, pixels, topdown=False):
    """pixels[y][x] = (r, g, b), y from the TOP.  A 24-bit BMP."""
    rowbytes = (w * 3 + 3) & ~3
    rows = []
    order = range(h) if topdown else range(h - 1, -1, -1)
    for y in order:
        row = bytearray()
        for x in range(w):
            r, g, b = pixels[y][x]
            row += bytes((b, g, r))          # BMP stores BGR
        row += bytes(rowbytes - len(row))
        rows.append(bytes(row))
    data = b"".join(rows)
    hh = -h if topdown else h
    info = struct.pack("<IiiHHIIiiII", 40, w, hh, 1, 24, 0, len(data),
                       2835, 2835, 0, 0)
    off = 14 + len(info)
    hdr = struct.pack("<2sIHHI", b"BM", off + len(data), 0, 0, off)
    return hdr + info + data


def bmp8(w, h, pixels):
    """The same picture 8 bits deep with a palette, bottom-up."""
    pal, idx = [], {}
    for y in range(h):
        for x in range(w):
            c = pixels[y][x]
            if c not in idx:
                idx[c] = len(pal)
                pal.append(c)
    rowbytes = (w + 3) & ~3
    rows = []
    for y in range(h - 1, -1, -1):
        row = bytearray(idx[pixels[y][x]] for x in range(w))
        row += bytes(rowbytes - len(row))
        rows.append(bytes(row))
    data = b"".join(rows)
    paldata = b"".join(struct.pack("<4B", b, g, r, 0) for (r, g, b) in pal)
    info = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 8, 0, len(data),
                       2835, 2835, len(pal), 0)
    off = 14 + len(info) + len(paldata)
    hdr = struct.pack("<2sIHHI", b"BM", off + len(data), 0, 0, off)
    return hdr + info + paldata + data


def index4(rgb):
    """graphics/Sprite.c, SPRITE LOADBMP."""
    r, g, b = rgb
    return ((r & 0x80) >> 4) | ((g & 0xC0) >> 5) | ((b & 0x80) >> 7)


def run(path, args):
    p = subprocess.run([LOADIMAGE, "-s", path] + [str(a) for a in args],
                       capture_output=True)
    if p.returncode != 0:
        return None, p.stderr.decode().strip()
    return p.stdout, None


def check(name, path, args, want_w, want_h, want):
    out, err = run(path, args)
    if out is None:
        print("  %-28s FAILED: %s" % (name, err))
        return 1
    w = out[0] | (out[1] << 8)
    h = out[2] | (out[3] << 8)
    got = list(out[4:])
    if (w, h) != (want_w, want_h):
        print("  %-28s size %dx%d, wanted %dx%d"
              % (name, w, h, want_w, want_h))
        return 1
    if got != want:
        print("  %-28s pixels differ" % name)
        print("     got  %s" % got)
        print("     want %s" % want)
        return 1
    print("  %-28s %dx%d, %d pixels correct" % (name, w, h, len(got)))
    return 0


def main():
    tmp = sys.argv[2] if len(sys.argv) > 2 else "/tmp"
    # Colours that land on every boundary that matters: 0x80 is the
    # threshold for red and blue, and green's top two bits give four
    # levels, so a quantiser used by mistake would show up here.
    P = [
        [(255, 255, 255), (0, 0, 0), (255, 0, 0), (0, 0, 255)],
        [(0, 255, 0), (0, 128, 0), (0, 64, 0), (0, 192, 0)],
        [(127, 127, 127), (128, 128, 128), (255, 255, 0), (0, 255, 255)],
    ]
    w, h = 4, 3
    flat = [index4(P[y][x]) for y in range(h) for x in range(w)]

    f24, ftd, f8 = tmp + "/g24.bmp", tmp + "/gtd.bmp", tmp + "/g8.bmp"
    open(f24, "wb").write(bmp24(w, h, P))
    open(ftd, "wb").write(bmp24(w, h, P, topdown=True))
    open(f8, "wb").write(bmp8(w, h, P))

    bad = 0
    bad += check("24-bit, whole picture", f24, [], w, h, flat)
    bad += check("top-down, whole picture", ftd, [], w, h, flat)
    bad += check("8-bit palette", f8, [], w, h, flat)
    want = [index4(P[y][x]) for y in (1, 2) for x in (1, 2)]
    bad += check("window 1,1 2x2", f24, [1, 1, 2, 2], 2, 2, want)
    want = [index4(P[y][x]) for y in (1, 2) for x in (2, 3)]
    bad += check("origin 2,1 to the edge", f24, [2, 1], 2, 2, want)
    # The reference's bound: a window that runs off the picture is
    # StandardError(34), the word "Coordinates".
    for args in ([0, 0, 5, 3], [0, 0, 4, 4], [4, 0], [-1, 0]):
        out, err = run(f24, args)
        if out is not None or "Coordinates" not in (err or ""):
            print("  %-28s wanted Coordinates, got %r"
                  % ("refuses %s" % (args,), err))
            bad += 1
        else:
            print("  %-28s Coordinates" % ("refuses %s" % (args,)))
    print("bmptest: %s" % ("all correct" if bad == 0 else "%d WRONG" % bad))
    return 1 if bad else 0


sys.exit(main())

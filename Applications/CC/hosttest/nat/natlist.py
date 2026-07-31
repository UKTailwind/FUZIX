"""List a bytecode object's code symbols and which are native.

  python3 natlist.py prog.bc
"""
import struct, sys

data = open(sys.argv[1], "rb").read()
magic, ver, pad, nsym, codesz, datasz, bsssz, entry, nfix, strsz = \
    struct.unpack_from("<4sBBHIIIIII", data, 0)
assert magic == b"FBC1"
HDR, FIXSZ, SYMSZ = 32, 8, 12
symoff = HDR + codesz + datasz + nfix * FIXSZ
stroff = symoff + nsym * SYMSZ
strtab = data[stroff:stroff + strsz]

def label_sym(name):
    """cc2's generated code symbols: "L<n>..." jump labels and
    "Sw<n>_<n>" switch case entries.  Not functions."""
    import re
    return re.fullmatch(r"L\d+\S*|Sw\d+_\d+", name) is not None

nat = tot = 0
for i in range(nsym):
    value, nameoff, styp = struct.unpack_from("<IIB", data, symoff + i * SYMSZ)
    if styp != 0:
        continue
    name = strtab[nameoff:strtab.index(b"\0", nameoff)].decode()
    if label_sym(name):
        continue
    isnat = value < codesz and data[HDR + value] == 0xF0
    tot += 1
    if isnat:
        nat += 1
    print("%-24s %6d  %s" % (name, value, "NATIVE" if isnat else "bytecode"))
print("version %d: %d of %d functions native" % (ver, nat, tot))

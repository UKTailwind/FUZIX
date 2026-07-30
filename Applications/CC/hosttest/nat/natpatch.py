"""Overwrite a bytecode function with native Thumb code.

  python3 natpatch.py prog.bc funcname code.bin out.bc

Writes the BC_NATIVE marker at the function's entry, pads to the next
even offset, lays the machine code down, and bumps the object to
version 2 so pre-mixed-mode interpreters reject it cleanly.  Refuses
to run past the next code symbol - the native body must fit inside
the bytecode stub it replaces.
"""
import struct, sys

BC_NATIVE = 0xF0

prog, name, binfile, out = sys.argv[1:5]
data = bytearray(open(prog, "rb").read())
native = open(binfile, "rb").read()

magic, ver, pad, nsym, codesz, datasz, bsssz, entry, nfix, strsz = \
    struct.unpack_from("<4sBBHIIIIII", data, 0)
assert magic == b"FBC1", "not a bytecode object"

HDR = 32
FIXSZ = 8
SYMSZ = 12
symoff = HDR + codesz + datasz + nfix * FIXSZ
stroff = symoff + nsym * SYMSZ
strtab = bytes(data[stroff:stroff + strsz])

target = None
codesyms = []
for i in range(nsym):
    value, nameoff, styp = struct.unpack_from("<IIB", data, symoff + i * SYMSZ)
    if styp != 0:               # BC_SYM_CODE
        continue
    codesyms.append(value)
    symname = strtab[nameoff:strtab.index(b"\0", nameoff)].decode()
    if symname == name:
        target = value
assert target is not None, "no code symbol " + name

limit = min([v for v in codesyms if v > target] + [codesz])
entry_off = (target + 3) & ~1           # BC_NATIVE_ENTRY()
need = (entry_off - target) + len(native)
assert target + need <= limit, \
    "native body (%d bytes) will not fit the stub (%d)" % (need, limit - target)

code_at = HDR + target
data[code_at] = BC_NATIVE
for i in range(target + 1, entry_off):
    data[HDR + i] = 0
data[HDR + entry_off:HDR + entry_off + len(native)] = native
data[4] = 2                             # BC_VERSION_NATIVE

open(out, "wb").write(data)
print("%s: %s at %d, native %d bytes at %d, version 2"
      % (out, name, target, len(native), entry_off))

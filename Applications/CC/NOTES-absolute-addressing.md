# One kind of address, 2026-08-03

A program address used to be an offset into `mem[]`. It is now a
machine address. This note is why, and what it cost.

## Why

mmb2c puts every array and string in one block taken from the kernel's
PSRAM heap, because 48K of VM address space cannot hold a 38,400 byte
array and never could. That block is at 0x11000000, nowhere near
`mem[]`, so the runtime had to tell two kinds of address apart:

    static unsigned char *vptr(unsigned long a)
    {
        if (a >= MEMSIZE) return (unsigned char *)a;
        return mem + a;
    }

That works for every path written in C, and it is not enough, because
**bcrun has a native backend**. `backend-thumb.c` is included into
`backend-bcode.c`, `have_native` marks the object `BC_VERSION_NATIVE3`,
and functions behind a `BC_NATIVE` marker run as Thumb under
`native_enter` with **r6 = mem**. Generated code reaches memory as

    ldr r3, [r6, r2]

in hardware. There is nowhere to put a test. A heap pointer arriving
there computes `mem + 0x11xxxxxx` and the machine dies with a precise
bus fault whose BFAR is exactly that sum -- which is how it was found.

Chasing it through the C paths one at a time cost two board flashes and
two filesystem rebuilds, and would never have converged: `lib_strchr`,
`lib_fgets`, `sprintf`'s `emit` and `vstrlen` were all genuinely wrong
and fixing all four changed nothing, because the fault was never in
them.

## What changed

The loader relocates the program to where `mem[]` actually is, once.
`database` and `bssbase` become absolute, and since every program
address the fixups produce comes through `symval()`, which adds one of
those two, the whole program is relocated in one place. `heap_top`,
`sp` and `mmrt_reserve`'s ceiling move with it.

The native backend needed **no change at all**: `native_enter` passes
`r6 = NULL`, so `ldr r3, [r6, r2]` becomes plain absolute addressing and
the backend's two conversions, `subs r2, r4, r6` and `adds r0, r6, r0`,
degrade to identities. Objects compiled before the change still run.
r6 is now a spare register the backend could reclaim.

`vptr`, `VM_OOB`, `VM_OOBN`, `mm_ptr` and `mm_off` all become identities
and stay only as names at the call sites.

On a 64-bit development host a VM address still has to fit in 32 bits,
so `mem` is mmapped low (`MAP_32BIT` where it exists, a hint plus a
check where it does not) instead of being a static array.

## Cost

Nothing bounds-checks a program address any more. That was already true
of every heap pointer, and the alternative was a compare and a branch on
every load and store in generated code.

## Measured, PC2, the 3,200-line eclipse compiled on the board

| build | time |
|---|---|
| offsets, arrays in VM space | 3.307 s |
| absolute, arrays in VM space | 2.782 s |
| absolute, arrays and strings in PSRAM | 2.785 s |

Two results worth keeping:

* dropping the offset arithmetic is worth **16%** on its own, and bcrun
  got 2.4 KB smaller.
* the heap split costs **nothing measurable** -- 0.1%, inside the noise.
  The eclipse's arrays are about 3 KB and the XIP cache is 16 KB, so
  PSRAM's 12 MB/s against SRAM's 44 never shows up. That will not hold
  for an array that outruns the cache; measure again for the
  framebuffer.

For scale: MMBasic 12.5 s, MicroPython 8.77 s on the same chip.

## Gates

`fcc/fcctests.sh` proves the bytecode path only -- the x86 bcrun never
executes a byte of translated Thumb. **`fcc/qemutests.sh` is the one
that matters here**: it rebuilds every test and runs it native under
qemu-arm. It caught `helper_call`'s `sp = vsp - mem` (0 of 11 passing,
segfaults) before any of this reached hardware, and qemu-arm is 32-bit
so `mm_heap` takes the real-pointer path there too. Run it before
flashing anything that touches addressing.

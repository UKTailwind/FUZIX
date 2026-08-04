# Building Fuzix for the Pico Computer 2 / 3, from a clean clone

Everything here comes out of this repository. The two things a user
needs are `build/fuzix.uf2` (the kernel) and
`Images/rpipico/pc3-sd-cc.img.gz` (the SD card); this describes how to
produce both.

`README.md` in this directory is upstream's, describing the generic
RP2040 Pico. This file is the PC2/PC3 build.

## Prerequisites

    arm-none-eabi-gcc, -binutils, -newlib     the cross toolchain
    make, cmake, git                          the build
    sfdisk (util-linux)                       partitioning the card
    pandoc + a LaTeX engine                   only to rebuild the manual

The Pico SDK is fetched automatically: `PICO_SDK_FETCH_FROM_GIT = yes`
in this directory's Makefile, so the first configure needs network. If
you have a local SDK, point `PICO_SDK_PATH` at it instead.

Note if you are rebuilding: cmake keeps `-DTOTALMEM` in
`build/CMakeCache.txt`. Deleting that cache without network makes cmake
try to re-fetch the SDK, and deleting it *with* a stale environment
loses the memory size. Clear everything in `build/` except `_deps`.

## 1. The host tools

`mkfs`, `ucp` and `fsck` build and write the Fuzix filesystem, and
`mkftl` makes the flash image. They are ordinary host programs:

    make -C Standalone

## 2. The kernel

From this directory:

    make TARGET=rpipico SUBTARGET=pico2

`SUBTARGET=pico2` is what selects the RP2350 and, with it,
`TOTALMEM=312` — the amount of SRAM given to user processes. The
result is `build/fuzix.uf2`.

312 rather than 320 because the kernel has taken two 4 KB bites out of
the pool: one to stop `progbase` floating as the kernel's size changes,
one to pay for the C library's allocator, which is the PSRAM heap
(`arena.c` overrides `_sbrk`) and is RAM-resident because the kernel is
`PICO_COPY_TO_RAM`. `linker_overrides/memory_ram.incl` carves the same
split and **nothing checks that the two agree**, so change both or
neither.

## 3. The userland

Every program on the card is statically linked, so a C library change
reaches them only by rebuilding them:

    sh relink-userland.sh

That covers `Applications/util`, `V7`, `MWC`, the games, `levee`,
`bbcbasic`, `mmedit`, `cpp`, `CC`, and this platform's own `utils`. It
deletes the objects first — a stale `.o` links against the old library
and looks like a successful build.

Then stage the compiler and the two image programs for installation:

    sh ../../../Applications/CC/hwtest/stageall.sh

## 4. The card

Three steps, in order. The first is only needed once — or whenever you
want to start from nothing:

    sh mkcard.sh          # empty, partitioned: Images/rpipico/pc3-sd.img
    sh mksdimage.sh       # builds the Fuzix root into partition 2
    bash ../../../Applications/CC/mkccimage.sh   # adds the compiler

giving `Images/rpipico/pc3-sd-cc.img` and a `.gz` beside it.

`mkcard.sh` refuses to overwrite an existing image, so remove
`pc3-sd.img` first if you mean to rebuild it.

The layout, which the kernel and both scripts assume:

    p1  0x0C  LBA   2048  131072 sectors   64 MB  FAT, for interchange
    p2  0x83  LBA 133120   65536 sectors   32 MB  Fuzix root  (hdb2)
    p3  0x7F  LBA 198656    8192 sectors    4 MB  reserved

Partition 2 starting at sector 133120 is not decorative: both image
scripts `dd` the filesystem to that sector, and the boot device name
`hdb2` refers to it. The root filesystem itself is 64000 blocks, not
65536 — block numbers are 16 bit and 65535 is also the "no such block"
marker, so the filesystem stops clear of both.

Partition 1 is left unformatted on purpose; the manual tells the user
to format it as FAT/FAT32 from Windows.

## 5. Checking it

    sh ../../../Applications/CC/hwtest/verifyimage.sh

lists what actually landed. It is worth running: `mkccimage.sh` refuses
on a `ucp` error and fsck's the result, but neither notices a file that
is present and *stale*, and a stale
`/usr/lib/cc/include/mmb_runtime.h` makes the on-board `cc` reject
generated programs with "type mismatch".

## 6. Installing

Kernel: hold BOOT, click RESET, release BOOT, and copy `fuzix.uf2` to
the drive that appears. From a running system, `sync; remount -n / ro;
sync` then `picoctl flash` gets there without touching the board.

Card: write `pc3-sd-cc.img` to an SD card of 128 MB or more with any
raw image tool. The whole card is overwritten.

**Both together.** The card's binaries are statically linked, so a new
kernel with an old card still runs the old C library.

## 7. The release number

Two version numbers are on screen at boot and they are different
things:

    FUZIX version 0.5                    upstream's, from Kernel/version.c
    Pico Computer 3, release 0.7         ours, PC3_RELEASE in config.h

Both are right. Before this they were not both printed, so a user who
downloaded `pc3-v0.6` and booted it saw only `0.5` and concluded the
wrong file had been published. **Bump `PC3_RELEASE` in `config.h` when
tagging a release**; nothing checks that the tag and the constant agree.

## 8. The manual

    pandoc FUZIX-PC3-MANUAL.md -o FUZIX-PC3-MANUAL.pdf

## What is not in this repository

`mmb2c`, the MMBasic-to-C translator, is developed separately: the
Python reference implementation and its test suite live there. What is
needed to *build* is here — `Applications/CC/mmbc_*.c` are verbatim
copies, kept in step by that repo's `fcc/sync-mmbc.sh`, and the same
goes for `mmb_runtime.c/.h` via `fcc/sync-runtime.sh`. Nothing in this
build reaches outside the tree.

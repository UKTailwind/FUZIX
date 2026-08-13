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
`TOTALMEM=340` — the amount of SRAM given to user processes. The
result is `build/fuzix.uf2`.

340 of the 512 KB, because the kernel keeps the other 172. It used to
keep more: it was built `PICO_COPY_TO_RAM`, which copied all 90,676
bytes of `.text` into RAM at boot. Most of it now executes from flash
(`linker_overrides/default_text_excludes.incl` names what may, and why
some things may not), which freed 45 KB and let the pool grow from 312
to 340.

`linker_overrides/memory_ram.incl` carves the same split and **nothing
checks that the two agree**, so change both or neither.

**The kernel's RAM is effectively full.** As of 2026-08-07 it uses
175,792 of its 176,128 bytes — **336 bytes spare**, where a year's
worth of these notes used to say 9,320. Assume any new kernel array,
buffer or statically allocated struct will not fit, and that the next
one to be added is the one that fails. The failure is at least loud: a
`region RAM overflowed` at link time, which is exactly what the split
was designed to produce. The answer when it happens is to move more
code to flash — add functions to `default_text_excludes.incl` — rather
than to take memory back from the process pool. To see where you
stand:

    arm-none-eabi-nm build/fuzix.elf | grep __bss_end__

against the RAM length in `build/fuzix.elf.map`'s memory configuration
(0x2b000).

## 3. The userland

Every program on the card is statically linked, so a C library change
reaches them only by rebuilding them:

    sh relink-userland.sh

It rebuilds **the C library itself first, from clean**, and then
`Applications/util`, `V7`, `MWC`, the games, `levee`, `bbcbasic`,
`mmedit`, `cpp`, `CC`, and this platform's own `utils`. Objects are
deleted before each — a stale `.o` links against the old library and
looks like a successful build. The libc step is not optional and was
not always there: without it the script relinked everything against a
library it never rebuilt, which shipped a card whose `df` still had the
old, too-small superblock buffer and brought the machine down.

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

The layout as shipped, for a 1 GB card:

    p1  0x0C  LBA    2048   262144 sectors  128 MB  FAT, for interchange
    p2  0x83  LBA  264192  1638400 sectors  800 MB  Fuzix root  (hdb2)
    p3  0x7F  LBA 1902592     8192 sectors    4 MB  reserved

**`mkcard.sh` is the only place that layout is written down.** It takes
`FAT_MB`, `ROOT_MB` and `RES_MB` (128 / 800 / 4 by default, 933 MiB in
total, which fits any card sold as 1 GB), and everything downstream
reads the geometry back out of the image's own MBR through
`p2geom.sh` — `mksdimage.sh`, `mkccimage.sh` and `verifyimage.sh` all
do. Nothing but `mkcard.sh` should ever contain a sector number.

The root filesystem fills partition 2 exactly, and its inode count
scales with it: `mksdimage.sh` allocates one inode per 64 blocks —
25,600 for the 800 MB root — clamped to the format's own 65,535 limit,
and `INODES=n` overrides it.

It did not always fill the partition. Under the old format a block
number was 16 bits, so 65535 was both the last possible block and the
"no such block" marker, and the filesystem stopped clear of both at
64000 blocks. FS32's marker is 0xFFFFFFFF and unreachable, so the
margin is unnecessary; see `FS32-FORMAT.md`, which is the authority for
the on-disk format.

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

Card: write `pc3-sd-cc.img` to an SD card of **1 GB or more** with any
raw image tool. The whole card is overwritten.

**Both together, and from v0.9 the machine insists.** The card's
binaries are statically linked, so a new kernel with an old card runs
the old C library — and since the FS32 change the two formats refuse
each other outright: a v0.9 kernel will not mount a pre-v0.9 card and
names it as the reason, rather than misreading it.

## 7. The release number

Two version numbers are on screen at boot and they are different
things:

    FUZIX version 0.5                    upstream's, from Kernel/version.c
    Pico Computer 3, release 0.7         ours, PC3_RELEASE in config.h

Both are right. Before this they were not both printed, so a user who
downloaded `pc3-v0.6` and booted it saw only `0.5` and concluded the
wrong file had been published. **Bump `PC3_RELEASE` in `config.h` when
tagging a release**; nothing checks that the tag and the constant agree.

`PC3_RELEASE` is not the only place the number is written down, and
that cost three releases:

    config.h                PC3_RELEASE "0.14"        the boot banner
    Applications/CC/        MM_RELEASE 0.14           what MM.VER
      mmb_runtime.h                                   answers in BASIC
    FUZIX-PC3-MANUAL.md     date: "Release v0.14 ..." the PDF's cover

`MM_RELEASE` was set when `MM.VER` was added at v0.10 and was still
0.10 at v0.13, so every BASIC program that asked the machine what it
was running was told something three releases old. The comment on it
asked to be kept in step with `PC3_RELEASE`; a comment is not a
mechanism. **Run**

    sh relcheck.sh

which reads all three and exits 1 if they disagree. Two things it
knows that the eye does not: `MM.VER` is MMBasic's `major.mmpp`, so the
minor part is padded to two digits and `0.9` there is `0.09`; and the
master copy of `mmb_runtime.h` is in the mmb2c repository, so fix it
there and re-run `fcc/sync-runtime.sh` or the next sync puts the stale
number back.

## 8. The manuals

There are two, and both are plain pandoc:

    pandoc FUZIX-PC3-MANUAL.md -o FUZIX-PC3-MANUAL.pdf
    pandoc PC3-C-MANUAL.md     -o PC3-C-MANUAL.pdf

**Appendix C of the user manual is half generated.** Its two tables —
the statements and functions `mmbc` translates — come from the
translator's own dispatch and `BUILTINS` tables, so they cannot drift
from what the program actually does:

    python3 ../../../../mmb2c/fcc/coverage.py            # the markdown
    python3 ../../../../mmb2c/fcc/coverage.py --check    # just the counts

Regenerate and paste when coverage changes. Everything *below* the
tables — what is done and not done in graphics, pins and sound — is
written by hand and the generator knows nothing about it, so replacing
the whole appendix with the generator's output throws that away. Check
the tables against `--check` (100 statements, 90 functions, 5 scalar and
6 array `MATH` at v0.14; 87 and 85 at v0.12) rather than pasting blind.

The splice is by *marker* — from `## Statements` down to, but not
including, `## MATH sub-commands`. An earlier script did it by line
number and went stale the moment anything above the appendix moved.

## What is not in this repository

`mmb2c`, the MMBasic-to-C translator, is developed separately: the
Python reference implementation and its test suite live there. What is
needed to *build* is here — `Applications/CC/mmbc_*.c` are verbatim
copies, kept in step by that repo's `fcc/sync-mmbc.sh`, and the same
goes for `mmb_runtime.c/.h` via `fcc/sync-runtime.sh`. Nothing in this
build reaches outside the tree.

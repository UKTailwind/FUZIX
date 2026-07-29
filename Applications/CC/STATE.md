# On-target compiler: state of play

2026-07-29. Read this and PC3-COMPILER-PLAN.md before touching anything.
The plan explains *why* the design is what it is; this says where the
work actually stands and how to run it.

## Where it stands

**C compiles to bytecode and runs, on the host and on the PC3.**

    cpp -> cc0 -> cc1 -> cc2 (backend-bcode.c) -> .bc -> bcrun

Phases 1-3 of the plan are done and verified on hardware. There is no
assembler and no linker; cc2 writes a binary object directly and bcrun
interprets it. `bcdump` disassembles.

Verified working, output identical to gcc: integer arithmetic of every
width, all control flow including `switch`, functions, recursion,
function pointers, pointers and arrays, structs, strings, casts,
globals/locals/arguments, `long long` declarations, storage, loads,
stores, shifts, arithmetic, comparisons and literals.

Runtime library: `malloc`/`calloc`/`free`/`realloc`, the `str*` and
`mem*` families, `atoi`, `abs`, `printf`/`puts`/`putchar` with width
and flags, `open`/`creat`/`close`/`read`/`write`/`lseek`/`unlink` onto
real Fuzix descriptors, and `time_us`/`adval` through the kernel's
PICOIOC ioctl.

Measured on hardware: 2000-element sieve in ~75 ms, against ~1 ms
interpreted on the development host.

## Next task: double

See "What double still needs" in PC3-COMPILER-PLAN.md - it has the
constants worked out, which is the part that costs time to rediscover.
Three steps, in order:

1. a double literal encoder in cc0 (the real work; the existing one is
   single precision end to end and its fraction table stops at 1e-9);
2. stop `target_type_remap()` folding DOUBLE onto FLOAT in
   target-thumb.c;
3. float and double opcodes, following the 64-bit integer pattern.

`samples/ll2.c` and a double equivalent are the cases to drive it.

## How to run things

Host tools, which is where all development happens:

    make -f Makefile.host              # cc0, cc1, cc2 into host-armm0/
    cc -o host-armm0/bcrun bcrun.c     # and bcdump.c the same way

`Makefile.host` builds the *armm0 type model* natively so the front end
can be exercised without hardware. `CPU=z80` builds the 16-bit model
for comparison.

Differential test - gcc is the oracle:

    cd hosttest && bash optest.sh samples/optest.c

It builds the same source with gcc (`-std=gnu89 -funsigned-char`, both
needed) and through our chain, diffs the output, and reports which
opcodes were exercised. A difference is a bug, not a judgement call.
This found four real defects in its first few minutes, one of them in
shared frontend code affecting every FCC target.

Cross build for the target:

    make -f Makefile.armm0 FUZIX_ROOT=/path/to/FUZIX USERCPU=armm0 bcrun

Then strip it - unstripped is 75K, stripped 14K, which matters when the
only way onto the board is the serial console.

## Getting things onto the PC3

The console is COM11 at 115200. **xmodem does not work**: `rx` polls the
console for a keypress to cancel, and on this machine the console *is*
the link, so the first byte of the transfer reads as a cancel. It is
built for a machine with a second serial port (`/dev/tty2` on GP0/GP1
exists if a second adapter is ever wired up).

Use uuencode over the console instead - `uusend.py` in the session
scratchpad. `/bin/uud` is on the card. 14K transfers in about 4
seconds now that the console is interrupt driven.

To reflash: `sync; remount -n / ro; sync` then `picoctl flash`, the
board appears as drive F:, copy the uf2. **Always remount read-only
first** or the card needs an fsck on every boot.

## Things that have cost hours

* **Never conclude the board is dead from silence.** After a reset it
  sits at the `bootdev:` prompt, and a bare CR does not re-prompt. Send
  `hdb2`, then `root`. Two separate occasions were misdiagnosed as a
  dead board when it was fine, one of them causing a working build to
  be overwritten.
* **The kernel build stamp only tracks main.c.** Editing any other file
  leaves the banner showing the old time. `touch main.c` before
  building if the stamp needs to be meaningful.
* **cmake does not track the linker `.incl` files.** `rm build/fuzix.elf`
  to force a relink after changing them.
* **WSL quoting eats `$variables` and multi-line heredocs.** Put shell
  loops in script files, or use the editor. `MSYS_NO_PATHCONV=1` stops
  Git Bash rewriting `/tmp`-style arguments into Windows paths.
* **The build needs `PICO_SDK_PATH`** pointing at
  `~/src/micropython/lib/pico-sdk`, plus `-DPICO_BOARD=pico2
  -DTOTALMEM=320`. It is not in the environment - it lives in
  `build/CMakeCache.txt`. Do not delete that cache without a network.
* **Header dependencies.** `Makefile.host` now depends on bytecode.h;
  without it cc2 keeps writing the old object layout while bcrun reads
  the new one, which looks exactly like a corrupt object.

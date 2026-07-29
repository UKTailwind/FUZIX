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

**Floating point works.** `float` and `double` arithmetic, comparisons,
the conversion matrix both ways and both signednesses, compound
assignment, globals, arrays, structs, arguments and returns - all
matching gcc, on the host and compiled on the PC3 itself.

The remaining gap is **`printf` has no `%f`**, so a program can compute
in floating point but cannot print it except by scaling and casting to
an integer, which is what `samples/fp.c` does. That is the next thing
worth doing.

**The compiler runs on the PC3 itself** (2026-07-29). `cc prog.c` on the
board drives cpp, cc0, cc1 and cc2 and writes a `.bc` that bcrun
executes. Nine samples - sieve, strs, rpn, libtest, ll2, width3, sw2,
dbl, fp - compile on the board and produce output identical to gcc.
cc0's literal encoding is byte for byte identical to the host's over
265 literals.

**`optest.c` is not among them**, and the reason is worth keeping:
cc1 rejects `(int)(&arr[5] - arr)` at line 136 with "type mismatch",
and the on-target driver stops on a cc1 error where `optest.sh` prints
it and carries on. An earlier run of this suite reported optest passing
on the board - that was the harness running a stale `.bc` left from a
cross-compiled transfer, since it did not delete the object first. It
does now.

Installed there: the three passes in `/usr/lib/cc`, and `cc`, `cpp`,
`bcrun`, `bcdump` in `/usr/bin`.

## Next task: struct passing by value

Read **"3. Struct passing and returning — RESUME HERE"** in
PLAN-c89-gaps.md. Struct *assignment* is done; passing was attempted,
failed cc2's `sp` balance check and was reverted, and the note records
the design to rebuild, what was written, and what to read before
writing any more of it.

## Done: the float and double opcodes

Steps 1 and 2 of the double work are done (see "Double" in
PC3-COMPILER-PLAN.md). cc0 encodes IEEE-754 doubles that match gcc bit
for bit, and `sizeof(double)` is 8.

What is left is step 3, the opcodes. Follow the 64-bit integer pattern:
the accumulator and slot handling already exist, so it is new opcodes
plus conversions to and from the integer types.

`hosttest/samples/dbl.c` is the test. It fails today, on purpose, and
`all.sh` has one line to delete when it starts passing. Every floating
operation currently emits a named runtime call that does not exist, so
bcrun says `no runtime function "addd"` and names what is missing -
`backend-bcode.c` does that deliberately, because `typesize(DOUBLE)` is
8 and without the guard a double `+` would quietly emit a 64-bit
integer add over the bit patterns.

## How to run things

Host tools, which is where all development happens:

    make -f Makefile.host              # cc0, cc1, cc2 into host-armm0/
    cc -o host-armm0/bcrun bcrun.c     # and bcdump.c the same way

`Makefile.host` builds the *armm0 type model* natively so the front end
can be exercised without hardware. `CPU=z80` builds the 16-bit model
for comparison.

Differential tests - gcc is the oracle throughout:

    cd hosttest
    bash all.sh                  # everything, one line per sample
    bash optest.sh samples/optest.c
    bash littest.sh              # the floating literal encoder
    bash littest.sh -r 5000 7    # ... plus 5000 random literals, seed 7

`optest.sh` builds the same source with gcc (`-std=gnu89
-funsigned-char`, both needed) and through our chain, diffs the output,
and reports which opcodes were exercised. A difference is a bug, not a
judgement call. This found four real defects in its first few minutes,
one of them in shared frontend code affecting every FCC target.

`littest.sh` compares the bits of every floating literal against what
gcc encodes for the same text, so it tests cc0 alone and needs no code
generator. Its random mode found the leading-zero float bug (`015.5`
parsed as octal 13 followed by `.5`) that nobody would have written by
hand. `all.sh` says which samples cannot use gcc as an oracle, and why,
rather than quietly skipping them.

    ../host-armm0/dumptokens < file.tok

prints the token stream, including the bits and the value of every
floating constant.

`hwlit.sh` prepares the same literal test for the board and its header
has the exact transfer and compare commands. Worth doing after anything
that touches the encoder: the host has a 64-bit `unsigned long` and the
board does not, and the encoder is built entirely out of 64-bit
arithmetic, so agreeing with gcc on the host does not settle what
happens on the machine it is for.

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

## Compiling on the board

    cc prog.c            -> prog.bc
    cc -v prog.c         show each pass
    cc -k prog.c         keep the .pp .tok .ir intermediates
    bcrun prog.bc

`ccbc.c` is that driver. It exists rather than being another `#ifdef` in
`ccfuzix.c` for two reasons: this target has no assembler and no linker,
so one source file is one program and the pipeline stops after cc2; and
`ccfuzix.c` under `CPU_armm0` is still configured to emit Z80.

**The passes cannot be driven from the shell.** cc1 and cc2 read back
from their own standard output, so it has to be opened `O_RDWR`. The
host harness spells that `1<>`; the Fuzix shell has no such redirection
and quietly leaves the pass reading the console instead, which looks
exactly like a hang.

cpp is not optional even for source with no directives in it: cc0 does
not know what a comment is - stripping them has always been cpp's job -
so without it the first comment in the file becomes a divide followed
by a multiply. `Applications/cpp/Makefile.armm0` was a `# TODO` stub;
it now includes `Makefile.common` like every other CPU, and builds.

There is still no libc for compiled programs and no headers, so declare
what you use (`int printf();`) - the runtime lives inside bcrun.

## Contained, not fixed: the board could not create files

2026-07-29, kernel commits 27a82379f and 9006603d5. **It was never a
compiler fault** - the kernel was allocating inodes that were live
files, because an inode was reaching the free list twice.

Full briefing note, written to be picked up cold:
**`Kernel/platform/platform-rpipico/NOTES-inode-freelist.md`**. Read
that rather than this paragraph if you are going near it. The short
version: one real fix (the kernel no longer trusts the free list stored
in the superblock) and two guards that make the remaining fault
harmless. The guards log when they fire, so a silent kernel means it is
fixed and these mean it is not:

    i_alloc: 664 in free list but in use, skipped
    i_free: 664 freed twice

Worth remembering even if you never touch the filesystem: **"File table
overflow" is a misleading message.** `filesys.c` maps every `i_open`
failure onto `ENFILE`, so that is what you see whatever went wrong. The
real error is the `i_open: bad inode` line above it.

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
* **Constants are `cval_t`, not `unsigned long`.** `unsigned long` is
  64 bits on the x86-64 host and 32 on the board, so the constant path
  was correct by accident and only on one of the two machines. Anything
  new that carries a literal must use `cval_t` or it will work cross
  compiled and truncate when self hosted - a difference no host test
  can see.
* **`make -f Makefile.armm0 <one target>` does not rebuild the others.**
  Two builds in a row produced byte-identical cc1 binaries after a
  header change, which looked like the change having no effect. It was
  a stale object. `rm -f *.o` when a header moves.
* **Build the tools, do not hand-build them.** `make -f Makefile.host`
  now builds bcrun, bcdump, dumptokens and ccbc as well as the passes.
  They used to be built by hand, so `rm -rf host-armm0` made every test
  fail at once in a way that looked like a compiler regression.

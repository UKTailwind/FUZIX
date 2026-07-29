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
`mem*` families, `atoi`, `abs`, `printf`/`sprintf`/`puts`/`putchar`
with width, precision and flags, `open`/`creat`/`close`/`read`/`write`/
`lseek`/`unlink` onto real Fuzix descriptors, and `time_us`/`adval`
through the kernel's PICOIOC ioctl.

**stdio** over those descriptors: `fopen`, `fclose`, `fread`, `fwrite`,
`fgetc`, `getc`, `fputc`, `putc`, `fgets`, `fputs`, `fprintf`, `feof`,
`fseek`, `ftell`, `rewind`, `fflush`, `remove`. A `FILE *` is the
descriptor plus one, which makes `stdin`/`stdout`/`stderr` the
constants 1, 2 and 3 - they have to be constants, because the object
format can import a *function* from the runtime but not a variable.
The same limit means **the address of a library function cannot be
taken**; bcrun refuses `&fprintf` by name at load time rather than
storing an index where a code address belongs.

Declarations for all of it are in `hosttest/ctest-include`, which is
what both test harnesses preprocess against.

Measured on hardware: 2000-element sieve in ~75 ms, against ~1 ms
interpreted on the development host.

**Floating point works.** `float` and `double` arithmetic, comparisons,
the conversion matrix both ways and both signednesses, compound
assignment, globals, arrays, structs, arguments and returns - all
matching gcc, on the host and compiled on the PC3 itself.

**`printf` has `%f`** as of 2026-07-29, with precision, width and the
flags, plus `sprintf`. It could not be handed to the host library the
way `%d` is - bcrun runs on the PC3 against Fuzix libc, whose printf
has no floating point at all - so the conversion is done by hand in
bcrun and checked against gcc by `samples/fmt.c`. One deliberate
difference, commented where it lives: rounding is half up, where a full
C library rounds half to even, so `%.2f` of 0.125 gives 0.13 rather
than 0.12. `samples/fp.c` still scales and casts, from before this
existed.

**The compiler runs on the PC3 itself** (2026-07-29). `cc prog.c` on the
board drives cpp, cc0, cc1 and cc2 and writes a `.bc` that bcrun
executes. **27 samples** pass on the host and everything tried on the
board has matched gcc there too - including `fmt` (which is what proves
the hand-written `%f` works on the target, the whole reason it could
not be delegated to libc). cc0's literal encoding is byte for byte
identical to the host's over 265 literals.

`optest.c` was the last holdout and now builds too. It was not a
pointer difference as previously recorded here: cc1 was rejecting
`apply(addfn, 9, 4)`, because a pointer to a function with an
unspecified argument list matched no prototype. See "Passing and
returning structs" below.

**The dialect is C89 plus declaration-after-statement.** That one C99
relaxation was taken deliberately: everyone writes it, refusing it is a
nuisance out of proportion to the standard, and it changes the meaning
of nothing that was legal before.

Installed there: the three passes in `/usr/lib/cc`, and `cc`, `cpp`,
`bcrun`, `bcdump` in `/usr/bin`.

## Done: struct passing and returning

Both work, on the host and compiled on the PC3. `hosttest/samples`
struct2 (assignment), struct3 (passing) and struct4 (returning) cover
them and all three build on the board.

Two facts govern all of it and are worth carrying to the bitfield work:

* **A struct valued expression is represented by its address.** Nothing
  can load an aggregate into the accumulator, so `hier1` and
  `call_args` keep both sides as addresses instead of calling
  `make_rval`, which would insert a dereference.
* **cc2 cannot size a struct.** `typesize()` returns 4 for anything it
  does not recognise, because struct sizes live in cc1's symbol table.
  Any aggregate operation must carry its length from the front end as
  an immediate - `BC_COPY` and `BC_PUSHN` both take a u16.

Returning is a hidden first argument holding the address of caller
allocated space; the function copies its result there and returns that
address, so the call's value is an address like every other struct
expression and `f().x` and `a = f()` fall out. The declarator is walked
before the base type is applied to it, so `type_parse_function` reads
the base type `do_type_name_parse` recorded rather than the return type
it does not have yet.

The trap that cost the first attempt: `gen_push` was adding
`stack_size()` - 4, the fallback - to the stack depth for an object it
pushed sixteen bytes of, while `T_CLEANUP` took back the real
`target_argsize`. That is what cc2's "sp" at the epilogue means.

## Conformance: 155 of 175 on the C89 subset

`bash hosttest/ctest.sh`, and read **PLAN-conformance.md** - the 20
remaining failures are categorised there by the cause we actually hit,
and it says what to do next. Nothing left compiles and then answers
wrongly: 10 are genuine C89 gaps, 2 are our own limits rather than
dialect, 8 are correctly refused as not C89.

**Next two, and they are the cheapest on the list.** Both are plain
C89, both are written constantly, and neither exists *at all*, which is
a different and worse thing than the subtle gaps around them:

* **unary plus.** `60 + +3` and `+5` are both rejected.
* **`typedef` inside a block.** Not just the anonymous enum it looks
  like in test 00198 - `typedef int myint;` in a block fails too.
  Typedefs are only handled in `toplevel()`, and `is_storage_word()`
  does not cover `T_TYPEDEF`, so a block does not even recognise one as
  the start of a declaration.

Three harness lessons are baked in and worth not relearning: the
suite's own dialect tags are unreliable (`ctestc89.sh` asks gcc with
`-std=c89 -pedantic-errors` instead), `optest.sh` compares the **exit
status** as well as the output - that blind spot is where the missing
implicit `return 0` for main lived - and it preprocesses our chain with
`-nostdinc -I hosttest/ctest-include` so a sample can use real headers
while gcc still builds the reference against the host's.

## The state of the kernel underneath

Two separate faults, both now fixed, and **both the same bug class**:
shared Fuzix code that is correct where `int` is 16 bits and wrong
where it is 32. That pattern is the single most useful thing on this
page. When something in the kernel misbehaves here and the code looks
obviously right, suspect the integer width before suspecting the logic.

**Fixed:** the inode double free, 2026-07-29, kernel commit
`d5f93d4d3`. It was never an inode bug - a two-byte overrun in
`blk_alloc` was overwriting the inode free-list count with a stale
value off the disk, because `s_nfree` is a `uint16_t` and the copy
length said `sizeof(int)`. `NOTES-inode-freelist.md`.

**Fixed:** `panic: no free buffers`, 2026-07-29. Never a leak. The LRU
scan in `freebuf()` computed a buffer's age as `bufclock - bf_time`
with both `uint16_t` and the running best in an `int16_t`. At 16-bit
int those promote to `unsigned` and wrap correctly; at 32-bit they
promote to *signed*, so after `bufclock` wraps every buffer has a large
negative age, fails the `>= oldtime` test, and the kernel panics with
the whole pool free. The dump caught it with 18 of 20 buffers reading
`busy 0`. `NOTES-buffer-panic.md`.

The clue that cracked it was the user's: *larger C files precede the
crash*. `bufclock` advances per buffer acquisition, so time-to-panic is
measured in buffer traffic, not wall clock - which also explains why it
died "while idle", the reading that had sent the hunt after a leak.

`devtools/bufwatch.py` and the `bufs` command reproduce and measure it
on demand; see the notes.

Neither ever blocked compiler work: the samples and the conformance
suite run on the host, and the board takes sustained compile loads
without trouble. They were a hazard when driving the board.

**Bitfields** remain the largest known C89 gap - "4. Bitfields" in
PLAN-c89-gaps.md - but PLAN-conformance.md now has evidence for what
actually matters, and two cheaper things come first.

## Passing and returning structs turned up two function pointer bugs

Both in shared front end code, both silent until the harness stopped
ignoring cc1:

* `int (*fp)()` - an unspecified argument list, which is how a callback
  is declared - matched no real function. `parse_function_arguments`
  records `()` as a lone ELLIPSIS, so the type code differs from any
  prototype's and every comparison called it a mismatch.
  `type_pointerconv` now accepts either side being unspecified when the
  return types agree.
* `&func` was incremented to a pointer to a pointer by `typeconv`'s
  function-to-pointer fixup, which did not check for the pointer it
  already was.

## Done: the float and double opcodes

All three steps of the double work are finished (see "Double" in
PC3-COMPILER-PLAN.md). cc0 encodes IEEE-754 doubles matching gcc bit
for bit, `sizeof(double)` is 8, and the arithmetic, comparisons and
conversion opcodes are in. `samples/dbl.c` and `samples/fp.c` pass on
the host and on the board.

Worth keeping from that work: `backend-bcode.c` deliberately routes any
floating operation it cannot generate to a *named* runtime call that
does not exist, so bcrun says `no runtime function "addd"` and names
what is missing. Without that guard a double `+` falls into the 64-bit
*integer* cases - `typesize(DOUBLE)` is 8 - and adds the bit patterns,
printing a wrong answer instead of stopping.

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

**It fails on a non-zero exit from cc1 or cc2.** It used to print their
errors and carry on, so the suite compared two outputs that were both
produced despite the front end having rejected part of the input - that
is how the two function pointer bugs above survived. Anything that
expects an error has to say so.

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

## Running the suite on the board

`devtools/hwbuild.py struct3 optest ...` sends each sample, compiles it
with the board's own `cc`, runs it under `bcrun` and diffs against the
gcc reference in `hwtest/`. `hwtest/mkref.sh <names>` captures those
references; `hwtest/stripall.sh` strips the cross built passes ready
for `uusend.py`.

It deletes the object before compiling. Without that a `cc` that fails
leaves the previous `.bc` in place and bcrun happily runs it, so the
suite reports a pass for a build that never happened - which it once
did, against a stale cross compiled object.

Sending new passes: `hwtest/stripall.sh`, then `uusend.py` each of
cc0, cc1, cc2, then `chmod +x` and move them into `/usr/lib/cc`.
Resend `bcrun` whenever `bytecode.h` changes or a program that passes
on the host will fail on the board with "bad opcode", which reads like
a code generator bug and is not one.

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

## Fixed: the board could not create files

2026-07-29, kernel commit `d5f93d4d3`. **It was never a compiler
fault**, and it was never really an inode fault either: `blk_alloc`
refilled the *block* free list with a length of `sizeof(int) + ...`
where the field is a `uint16_t`, so on this 32-bit target it copied two
bytes too many - straight onto `s_ninode`, the inode free list count,
which sits immediately after `s_free[]` in `struct filesys`. Every
refill restored a stale count and resurrected already-popped entries
naming live files.

Full write-up, including the five V7 divergences found on the way and
fixed alongside:
**`Kernel/platform/platform-rpipico/NOTES-inode-freelist.md`**.

The guards from the hunt are still in and still log, so a silent kernel
means it is behaving and these mean something has regressed:

    i_alloc: 664 in free list but in use (mode 81a4 nlink 1)
    i_free: 664 freed twice

Worth remembering even if you never touch the filesystem: **"File table
overflow" is a misleading message.** `filesys.c` maps every `i_open`
failure onto `ENFILE`, so that is what you see whatever went wrong. The
real error is the `i_open: bad inode` line above it.

## Things that have cost hours

* **A symptom that "cannot" be X is evidence about your model, not
  about X.** "It died while idle, so it must be a leak" was the load
  bearing assumption of the buffer hunt, and it was wrong: idle meant
  the pool was *free* and the panic fired anyway. Sessions went into
  auditing `bread`/`brelse` pairs that were balanced all along. The
  question that would have shortcut it is the cheap one - *is the thing
  the panic claims actually true?* Printing the pool answered it in one
  crash.
* **Ask what the counter is measured in.** `bufclock` counts buffer
  acquisitions, not time, so "dies while idle" and "large files precede
  the crash" are the same fact. The user supplied the second clue and it
  was worth more than every audit.
* **On this port, suspect the integer width first.** Both kernel faults
  found so far are shared Fuzix code that is correct at 16-bit int and
  wrong at 32 - `sizeof(int)` for a `uint16_t` field, and `uint16_t`
  operands promoting to signed. Neither looks wrong when you read it.

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
* **`funcbody` does not mean "inside a function body".** It is set
  *after* `function_body()` returns, to mean "one has just been
  parsed". Trusting the name cost two wrong turns during the struct tag
  scoping work. `in_funcbody` is the one that means what it says.
* **Symbol table position cannot tell you what scope a name is in.** A
  file scope symbol sits above `symtab` while `block_base` may still
  *be* `symtab`, so "above the mark" does not mean "declared in this
  block". The first tag scoping attempt swept every file scope struct
  in the program on that assumption. Ordinary locals get away with it
  only because the `< S_STATIC` storage class test protects everything
  else; anything outside that test needs its own flag.
* **A rejection hides whatever comes after it.** Allowing mixed
  declarations unblocked nine conformance tests and gained four - the
  other five had a second, unrelated gap waiting behind the first
  error. Do not estimate remaining work from failure counts.
* **The oracle decides, and that includes the exit status.**
  `optest.sh` threw gcc's away and only printed ours for a long time. A
  program's status is part of its answer, and that blind spot is
  exactly where the missing implicit `return 0` for `main` was living.
* **gcc in `-std=gnu89` is not a reference for everything.** It does
  not zero main's return either (that is a C99 rule), its `long` is 8
  bytes, and its headers are unparseable by a C89 compiler. Where the
  oracle disagrees by construction, say so in the sample and check the
  behaviour another way - `samples/mainret.c` and `ll` both do.
* **Read `%TEMP%\uusend.log` after any board death.** The transfer tool
  used to count the board's replies as flow control and discard them,
  which is how four crashes in a row said nothing. It logs now.

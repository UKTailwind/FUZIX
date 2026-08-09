# What is done, and what is outstanding

Started 2026-08-08 as a note about two emission changes; it now covers
the whole line of work that came out of them, because the interesting
findings were not in the emitter at all.  Newest state at the top of
each section.

## Shipped, each measured on the board

| change | where | measured |
|---|---|---|
| 32-bit SDIV fast path in `mm_idiv`/`mm_mod` | mmb2c `65421d4`, FUZIX `e045f41d5` | bench +0.9%, eclipse −2.25% |
| word-wise `memcpy`/`memset` for the ARM libc | FUZIX `d1e7e7945` | LOCAL-string calls 3.1× (13.25 → 3.77 µs) |
| then the whole string half from newlib instead | FUZIX `54279484c`, `8c442ec55`, `fbd73e456` | bench +6.0%, eclipse −3.4%, bubble −6.1%, compile 7→6 s |
| four C89 gaps in `cpp` | FUZIX `640597d97`, `d6cadf8ef` | board conformance 156 → 160 of 175 |
| cc1 signed constant folding (`-7/2` folded unsigned) | FUZIX `e18b966cd` | correctness |
| `as_flt` writes an int literal as a float | mmb2c `8b55ccc`, FUZIX `07a65eb22` | bytecode identical; frees ON ERROR programs from `mm_fdiv` |
| LOCAL frames from a bump arena | mmb2c `65421d4`+ | 2.2× hosted; nothing on the board (bcrun overrides `mm_lheap`) |

**Killed on the evidence, do not re-propose:**

* The `__mmb_main` wrapper. `main` has been Thumb-translated and
  entered natively since `962483175`, which is in v0.9 — `fn_is_main`
  only suppresses *reclaim*.  `THUMB_VERBOSE=1` says `native: main` for
  43 of 44 test programs.
* Inlining `MOD` and `\` by a literal divisor: measured **0.12%
  slower** on an adjacent A/B (51517 vs 51580 grains).  Both forms end
  in the same libgcc 64-bit division and the inline one adds the
  translator's helper routing.  It did expose the cc1 fold bug above.

## Outstanding

### 1. What is left of the libc substitution

Done, and the board and tree agree: every routine newlib can give this
target without dragging its innards along now comes from there, 21
objects, extracted from the toolchain's own libc.a at build time.  The
word-wise C written the day before is gone - newlib's tuned assembly
beat it, which is what the kernel has always used.

What deliberately stays ours, with the reason, so it is not
re-investigated: `strcasecmp` and the case/compare family (`_ctype_`),
`strdup`/`strndup` (`_impure_ptr`, `_malloc_r`), `strerror`,
`strsignal`, `strftime` and the whole `strto*` family (reentrancy
structures), `strsep` (`__strtok_r`), `strtok` (`_impure_ptr`), and
`strstr` — newlib's is 1488 bytes against a few dozen here for a
function nothing hot calls.

If any of those is ever wanted, the mechanical test is in
`Library/libs/Makefile.armm0`: does the object define the routine, and
does it reference nothing we cannot satisfy?  `nm` the archive
afterwards and look for duplicate `T` symbols — that is how the double
`memccpy` was caught. (`vfprintf` and `vfscanf` are doubled from
`vfprintf.c` and `vfprintf_m.c` both being listed; that predates all of
this and is left alone.)

### 2. What is left in cpp

`Applications/cpp/cpptest.sh` is the gate now — run it after any change
there.  Known remaining gaps, all recorded rather than hidden:

* A **function-like** macro inside an argument is substituted as
  written; `expand_arg_text()` only expands object-like ones.  The
  rescan after substitution still expands such a call everywhere except
  as an operand of `##`.
* Variadic macros (`__VA_ARGS__`) — C99, out of scope for a C89
  compiler.  c-testsuite 00084 and 00097.
* `#pragma push_macro`/`pop_macro` — a GNU/MSVC extension, deliberately
  not implemented: it is not C89, and it means copying variable-length
  entries out of a hash table that pages to disk.  c-testsuite 00206.
* `#line`'s optional file name is parsed and ignored; applying it would
  also have to unwind at the end of an `#include`.

### 3a. Runaway recursion can still crash the machine — NATIVE PATH

The urgent half of what follows.  `bcrun.c` now has a `stack_floor` and
`BC_ENTER` refuses to go below it, but **a translated function never
passes `BC_ENTER`**: its prologue is a bare `sub r4, #n` and a
self-call is a direct BL.  A small recursive SUB is exactly what the
translator takes, so the common case is the uncovered one — a
six-argument routine recursing without bound took the whole board down,
video and all, with the check in place (2026-08-09).

Two ways to cover it, neither a five-minute change, both wanting
`qemutests`:

* The prologue compares r4 against a floor and branches to a fault
  helper — two or three instructions on every native entry.  The floor
  could come from a new `native_helpers` slot (one `ldr`), or from r6,
  which `native_enter` currently sets to 0 — but r6 is still used for
  the absolute-addressing identities, so reclaiming it is not free.
* `cc2` declines to translate a function that calls itself.  Simpler,
  and recursion then runs interpreted and guarded — but it misses
  mutual recursion, and it makes recursive code slow on purpose.

Until one of them is done: **do not run unbounded-recursion probes on
the board.**  A sane depth is safe and reports honestly; runaway
recursion is undefined behaviour with the machine's video attached
to it.

### 3b. Recursion depth — DONE, 15 levels to 255

`MM_BYREFN` was 16, and a by-ref argument slot cannot be released until
the call returns, so `go n + 1` died at fifteen.  Now 256 (2K of the VM
region), measured on the board at 255 levels for every shape — no
LOCALs, scalar LOCALs, a LOCAL string, a string expression per level —
with `bench` and the graphics frame unmoved.  The hosted build's
separate nine-level wall is fixed too: the compound-literal path now
asks for the same release the `--fcc` path always did.

If more than 256 is ever wanted, the principled fix is to put by-ref
temporaries in the routine's own LOCAL frame, which nests with the
calls and spills to malloc — no wall at all, but an emitter change in
both translators.

### 3c. The original note: the temporary pool

Found 2026-08-09 while testing the LOCAL arena under recursion.  A
routine that builds a string expression can only recurse **9 levels**
before "String expression too complex - raise MM_TMPN".

The arena itself is fine - frames nest as the calls do, release is an
absolute restore rather than a decrement, so arena and malloc'd frames
interleave safely, and 40 levels are intact at arena sizes of 64, 4096
and 65536.  The cap is the separate 16-slot string temporary pool, and
the emitted code says why:

    mm_release(__mark);
    mm_sset(__L->v_tag, mm_scat(...));   /* temps taken here */
    if (...) { f_descend(...); }         /* recurse - no release */

`mm_release` is emitted before statements that USE temporaries, so the
previous statement's temporaries are still live across the call.  Each
level holds about two slots for the duration of everything below it.

Fix: emit the release before a statement that calls a routine as well,
which makes the depth irrelevant and costs nothing at run time - the
release is two stores.  Both translators, byte-identical, cgate 0.  The
alternative, raising MM_TMPN, costs 257 bytes of static RAM per slot
and only moves the wall.

A tree walk or a recursive-descent parser in BASIC hits this today, so
it is worth more than its size suggests.

### 4. The mmbc emission queue

Still unexamined: `mm_mark`/`mm_release` elision in routines with no
temporaries; int narrowing of loop counters; splitting SUBs that are
over the translated-size ceiling (`structtest`'s 32,556-bytecode main
line is the only one in the suite that bails, with `size policy`).

Rejected with reasons: `var = var + k` → `+=` (the eqop libcall
measured *faster*); `x^2` → `x*x` and INT() round-trip elision (both
change results, and different is worse than missing).

### 5. The long-session slowdown, parked

Not reproduced.  Bubble measured 78.812 ms on a fresh boot and 78.809
ms after a session of heavy compiling; bench was 52,052 grains before
and after a full eclipse compile.  So the cross-run "heap fragmentation"
hypothesis has no support from these two workloads.

If a user reports it again, the first place to look is the swapper: with
no MMU every process must run at the same address, so `pagemap_switch`
physically `memcpy`s 4K blocks on every context switch to put the
running process at the bottom of memory (platform-rpipico/swapper.c).
Counters there — blocks copied and swapped per switch, free-block runs —
would settle it in one measurement.  Note the user's report was of a
graphics program losing 15% (85 → 98 ms) after *editing and compiling*,
which my fixed-frame variant did not show.

### 6. Fold the board suite into the release checks

The c-testsuite now lives on the card at `/root/ct` with its runner
`ctb3.sh`, and `/root/t9` holds just the interesting failures.  It is
the only thing that has ever exercised `cpp`, and it found four bugs
the host gate structurally cannot see, so a release should run it.

Constraints of that environment, learned the hard way: the runner must
be plain Bourne — no `$(( ))`, no `${x%y}`; `cmp` has **no -s** and an
unknown option makes it fail, which reads as a mismatch on every test;
`rm -f` still reports a missing file; there is no `sed` and no gzip, so
the suite goes over as a plain 338K tar and `tar -x -f` is the spelling.
A long run needs the console held open for its whole duration
(`scratchpad/runlong.py`) — closing the port hangs up the tty and takes
the job with it — and the marker waited for must not appear in the
command line, or the echo of the command ends the wait immediately.

## Board reference numbers

v0.9: 49,499 grains / 2.311 s.

Current: bench **55,163** grains, solar eclipse **1.953 s**, bubble
**74.03 ms** a frame, a LOCAL-string call 3.31 µs, the eclipse compiles
in 6 s, board conformance 160 of 175.  Against v0.9 that is bench +11.4%
and the eclipse −15.5%.

Measure the way [[pc3-benchmark-method]] says: fresh boot, output to the
screen, the two builds interleaved in one session, and `cmp` the .bc
first if the claim is about the runtime.

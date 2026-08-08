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

### 1. Finish landing memmove  — do this first, the board is behind

`memmove_armm0.c` is written and tested (7056 overlap and alignment
cases against a byte-loop oracle) but **not committed**, and two board
binaries are older than the tree:

| binary | tree | board |
|---|---|---|
| `bcrun` | 86556 | 86276 — no word-wise memmove |
| `mmbc` | 94792 | 94276 — built against the old libc |

`memmove` matters more than its name suggests: bcrun's `ns_memcpy` —
the native slot a translated program's `memcpy` calls — routes through
it, because a compiled program may hand it overlapping regions.  So
compiled code's block copies are still byte-at-a-time on the board.

Steps: `git add Library/libs/memmove_armm0.c Library/libs/Makefile.armm0`;
send `bcrun` and `mmbc`; keep `.prev`; re-run `bubblet.bc` (78.81 ms is
the reference) and `sh /root/ct/ctb3.sh /root/t9`; then commit the six
installed binaries (`bcrun cc0 cc1 cc2 ccbc mmbc` under Applications/CC
and `cpp` under Applications/cpp) as the builds on the card.

### 2. Measure what the new libc did to compile times

Never taken.  The eclipse compiled in **7 s** (`date; cc
solar_eclipse.c; date`) with the pre-libc toolchain; cc0/cc1/cc2 were
installed after that, so the comparison is still owed.  The board's RTC
is fine for this — the "oscillator was stopped" line is the 32 kHz pin,
which this board does not connect.

### 3. The rest of the byte-loop libc

`memcmp`, `strlen`, `strcmp`, `strcpy`, `strcat` are still the generic
8-bit versions.  Ranked by who pays:

* `memcmp` — every MMBasic string comparison goes through it.
* `strlen`/`strcmp` — the compiler's symbol tables.  Note bcrun already
  has word-wise `ns_strcmp`/`ns_strlen` natives, so this is about
  cc0/cc1/cc2/mmbc rather than compiled programs.
* `strcpy`/`strcat` — smaller.

Same shape as the others: a `_armm0.c` beside the generic one, swapped
in `Makefile.armm0`, and **`ar d syslibarmm0.lib <old>.o`** after, or
the stale member wins the link (that trap cost an hour).
`Library/tests/memtest_armm0.c` is the pattern for the oracle test.

### 4. What is left in cpp

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

### 5. The mmbc emission queue

Still unexamined: `mm_mark`/`mm_release` elision in routines with no
temporaries; int narrowing of loop counters; splitting SUBs that are
over the translated-size ceiling (`structtest`'s 32,556-bytecode main
line is the only one in the suite that bails, with `size policy`).

Rejected with reasons: `var = var + k` → `+=` (the eqop libcall
measured *faster*); `x^2` → `x*x` and INT() round-trip elision (both
change results, and different is worse than missing).

### 6. The long-session slowdown, parked

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

### 7. Fold the board suite into the release checks

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

v0.9: 49,499 grains / 2.311 s.  Current: bench 52,052 grains, solar
eclipse 2.022 s (screen output, fresh boot), bubble 78.81 ms per frame,
board conformance 160 of 175.

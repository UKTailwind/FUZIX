# The queue

One ranked list, because the work was spread across five documents and
three of them had gone stale. Reviewed 2026-08-12, against the tree
rather than against the notes — everything below was checked by reading
the code or running it, and four items that were still listed as
outstanding turned out to be finished.

Detail lives in the documents named in each entry; this file is only
the order and the reason.

## Corrected on this pass — do not re-do these

| said | actually |
|---|---|
| COVERAGE Tier B: `ON ERROR SKIP/IGNORE`, `MM.ERRNO`, `MM.ERRMSG$` "deserves its own pass" | **Done.** It got the pass. Checked-flag route, `mm_err_bind`, and the checks are gated so a program that never traps pays nothing (+11.9% on the benchmark). `tests/onerror.bas`. |
| PLAN-pixel-batch: "DESIGN AGREED, not implemented" | **Done and board-verified**, ripple −21%. `mm_ptbuf`/`mm_pixn`/`mm_pix_drain` are all in mmb_runtime.c. |
| PLAN-emission-next 3c: string expressions cap recursion at 9 levels | **Done.** `mm_release` is emitted before call statements now; the same shape reaches 60+. `MM_TMPN` never changed, which was the point. |
| PLAN-emission-next 6: "there is no `sed`" on the board | There is, as of v0.13 — and `awk`, `find`, `expr` and a working `[`. The board test runner can stop contorting around their absence. |

## Next

**Games first (2026-08-13): BLIT, SPRITE, PLAY SOUND/TONE and PLAY
MODFILE/MODSAMPLE now have a full plan in PLAN-games.md and take
priority over the ranking below where they conflict. Phase 0 of that
plan (board spikes) is the next concrete action.**

### 1. `SPI WRITE` of a LONGSTRING

The 255-byte string limit is the one wall the display work keeps
hitting: a 240-pixel RGB565 row is 480 bytes, so every row goes out as
two writes, and a whole frame cannot be assembled in BASIC at all. The
kernel has no such limit — one syscall takes a frame — and LONGSTRING
already exists and is already a byte buffer inside an INTEGER array.

So this is a statement form, not a mechanism: teach `SPI WRITE` (and
`I2C2 WRITE`) to take a LONGSTRING and pass its bytes. Note the trap
recorded in COVERAGE — a LONGSTRING handed over as a numeric array
today sends one byte per 8-byte cell, silently.

Small, and it unblocks the framebuffer work that stalled on it.

### 2. Split SUBs over the translated-size ceiling

`structtest`'s main line is 32,556 bytecodes and bails with `size
policy` — the only program in the suite that does. That is a capability
limit rather than a performance one: a large program simply cannot be
translated, and the failure names an internal policy rather than
anything the author can act on.

At minimum the message should say what to do. Better is to split at
statement boundaries into `__main_1()`, `__main_2()`.

### 3. The rest of the `MATH` family

`M_MULT M_INVERSE M_TRANSPOSE M_DETERMINANT V_CROSS V_NORMALISE
MAGNITUDE DOTPRODUCT CORREL CHI CROSSING`, plus `MATH CRC` and
`BASE64`. Pure arithmetic, no platform dependency, each one small and
independently testable against the interpreter. Best added on demand
rather than as a block — the 3D and graphics demos say which are
wanted first.

### 4. Emission: the two remaining candidates

From PLAN-emission-next §4, both unexamined:

* `mm_mark`/`mm_release` elision in routines that take no temporaries
  at all — currently every routine pays the pair.
* int narrowing of loop counters.

Measure the way pc3-benchmark-method says: fresh boot, output to the
screen, both builds interleaved in one session.

### 5. `POKE` and `MEMORY COPY|SET|PACK`

The natural pair to `PEEK`, which shipped in v0.12, and the honest
argument for it is the same: a program driving its own display wants
to move bytes without a loop. The argument against is stronger than it
was for `PEEK`, and it is recorded in COVERAGE — a bad read kills one
program, a bad write can take the kernel with it, and there is no MMU
to disagree. **Wants a decision, not an implementation.**

### 6. Fold the board test suite into the release checks

PLAN-emission-next §6. The c-testsuite on the card at `/root/ct` is the
only thing that has ever exercised `cpp`, and it found four bugs the
host gate structurally cannot see. It should run before a release. Now
easier than when that was written, since the board has `sed` and `awk`.

## Wants your steer before any work

* **`REDIM [PRESERVE]`** — needs heap-allocated arrays instead of the
  current static ones, which puts `malloc` into generated code that has
  none today. A real change to the array model; only worth it if you
  use it.
* **`KEYDOWN`** — needs a key-state table from the kernel, which
  INKEY$'s one-byte read cannot give. Small ioctl plus a wrapper.
* **Interrupts phase 2** (PLAN-interrupts) — `COM` rx level via TIOCINQ,
  PLAY-done via SNDIOC_PCMOWNER. Both are reads of state the kernel
  already keeps.
* **`PEEK(VAR x)` / `VARADDR`** — asks about a *variable* rather than an
  address, so it needs the symbol table on the translator side.
* **`CALL(fname$)`**, **`JSON$`**, **`CSUB` as an extern declaration**,
  **`VAR SAVE`/`RESTORE`** — all in COVERAGE Tier C with their catches.

## Deliberately closed

`sort -k`, `diff -u` and `grep -E` are userland, not mmb2c, and are
recorded in the FUZIX tree. `__mmb_main` wrapper and inlining `MOD`/`\`
by a literal are killed on measured evidence — see the "do not
re-propose" table in PLAN-emission-next.

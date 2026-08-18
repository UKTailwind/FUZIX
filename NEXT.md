# The queue

One ranked list. **Reviewed 2026-08-18 against the tree** — every entry
below was checked by reading the code or running it, not by reading the
notes. The previous review (2026-08-12) had drifted the same way its own
predecessor had: of its six ranked items **two were finished and a third
half-finished**, and of its eight "wants your steer" items **four were
finished**.

Detail lives in the documents named in each entry; this file is only the
order and the reason.

**Language coverage is NOT ranked here.** `COVERAGE-STATUS.md` is
generated from MMBasic's `AllCommands.h` and mmb2c's own dispatch, so it
cannot go stale the way this file does. Take gaps from there; this file
carries only the work that a name-by-name list cannot express.

## Corrected on this pass — do not re-do these

Checked in the tree on 2026-08-18. Each names where to look, so the next
review can re-check rather than re-trust.

| said | actually |
|---|---|
| 1. `SPI WRITE` of a LONGSTRING | **Done.** `mmb2c.py:7687`, `mmc_tx_ls` — no cap, no copy. `I2C2 WRITE` shares it. |
| 2. Split SUBs over the translated-size ceiling | **Gone.** No `size policy` anywhere in `mmb2c.py` or `mmbc/`, and `structtest` builds and passes in `make check`. |
| 5a. `POKE` | **Done.** `mmb2c.py:4489`. The decision it "wanted" was taken. |
| `REDIM [PRESERVE]` | **Done.** `mmb2c.py:4485`. A dynamic array is a flat pointer plus a bounds table — the shape an array parameter already had, so no new array model was needed. |
| `KEYDOWN` | **Done.** `mmb2c.py:2258`, `BUILTINS` line 111. |
| `PEEK(VAR x)` / `VARADDR` | **Done.** `mmb2c.py:1571`, `tests/varaddr.bas`. |
| `CALL(fname$)` | **Done.** `mmb2c.py:1413` (the function form; the statement is 4395). |
| Interrupts phase 1 | **Done.** `SETTICK`, `ON KEY`, `SETPIN INTH/INTL/INTB`, and the SPRITE interrupt family. PLAN-interrupts' phase-1 table is fully struck through. |
| Scalar `PIXEL` batching | **Done and board-verified 2026-08-11**, ripple −21%. Listed here again because it was re-proposed on 2026-08-18 from a stale memory note. `mm_ptbuf`/`mm_pixn`/`mm_pix_drain`, 49 references in `mmb_runtime.c`. |

Also finished since the last review and not in any queue: `LOAD JPG`,
`LOAD PNG`, `SPRITE LOADPNG`, `ARRAY SLICE`, `ARRAY INSERT`,
`MATH SLICE`, `MATH INSERT`, `COLOUR MAP`, `DefineFont`, the
`OPTION BASE 1` initialiser fix, and the games plan in full
(`BLIT`, `SPRITE`, `PLAY SOUND/TONE`, `PLAY MODFILE`).

**Category 1 of COVERAGE-STATUS.md — "finish what is already there" —
is now empty.**

## Next

### 1. Pixel batching phase 2: the deferred tail

The only outstanding part of the batching work, and it is correctness,
not speed. `MM_PIX_LATENCY_US` is 10 ms, but the bound is only tested
inside `mm_pixel` — so a program that plots and then computes silently
leaves its last partial batch unpainted until it plots again. There is
no `alarm()` anywhere in `mmb_runtime.c`; the backstop was designed and
never built.

`SIGALRM`, and **100 ms, not 1 s**: `alarm()` is the POSIX seconds
wrapper, but `_alarm()` takes DECISECONDS and `process.c` decrements
once per decisecond tick, which is the only way a process can see it.
Needs a busy flag so the handler cannot land between the append and the
count update, and a test that plots and then sleeps.

Small, bounded, and it closes a visible-to-the-user gap.

### 2. `MATH CRC` and `BASE64`

The two most asked for of the 52 MATH members still out. Pure
arithmetic, no platform dependency, and testable against the
interpreter one function at a time. The matrix, vector, quaternion and
complex families are the same shape and are best added on demand — the
3D and graphics demos say which are wanted first, and adding the block
speculatively is how a translator grows code nobody calls.

See the MATH section of `COVERAGE-STATUS.md` for the full split.

### 3. Emission: the two remaining candidates

From PLAN-emission-next §4, both still unexamined, both verified
outstanding today:

* **`mm_mark`/`mm_release` elision.** Every generated routine opens with
  `unsigned __mark = mm_mark();` unconditionally (`mmb2c.py:7979` and
  `8652`), including routines that take no string temporary at all. The
  `tmp_used` flag already exists for per-statement release and is the
  obvious lever.
* **int narrowing of loop counters.**

Measure the way `pc3-benchmark-method` says: fresh boot, output to the
screen, both builds interleaved in one session. Expect small numbers and
be willing to drop either on the measurement.

### 4. `MEMORY COPY | SET | PACK`

`POKE` landed, which was the contentious half; this is the block form
and the argument is now only about speed, not safety. A program driving
its own display wants to move bytes without a loop.

### 5. Interrupts phase 2

Two reads of state the kernel already keeps, from PLAN-interrupts' own
table: **COM rx level** via `TIOCINQ` on the port, and **PLAY-done** via
polling `SNDIOC_PCMOWNER` back to 0. Neither exists yet — the
`SNDIOC_PCMOWNER` call already in `mmb_runtime.c` is `PLAY STOP` asking
who owns the stream, not an interrupt source.

Nothing for the RTC: the pin path covers it.

### 6. Fold the board test suites into the release checks

`relcheck.sh` checks the release number in three places and nothing
checks that the gates were run. Two of them are board-only by
construction and have each found bugs no host gate can see:
`Applications/cpp/cpptest.sh` (the C preprocessor — host gates use
`gcc -E`, so `cpp` is untested off the board) and the c-testsuite at
`/root/ct`.

A release checklist that names every gate, host and board, and refuses
without them. Now easier than when this was first written: the board has
`sed`, `awk`, `find`, `expr` and a working `[`.

## Wants your steer before any work

* **The PIO block** — 25 of the 29 names in COVERAGE-STATUS category 3,
  all or nothing, and a language inside the language. The single
  largest remaining piece of MMBasic.
* **`JSON$`** — a parser, and the only category-2 language item with
  real size to it.
* **`VAR SAVE` / `VAR RESTORE`** — wants somewhere to put the values and
  a decision about what survives a reboot.
* **`WATCHDOG` and `CPU`** — both are really kernel questions here: the
  kernel already runs a watchdog on core1, and `CPU RESTART`/`SLEEP` ask
  it to do something it may not want to do.
* **`RESOLUTION`, `REFRESH`, `GETSCANLINE`, `MEMORY`** — the rest of
  category 3, each a decision about the machine rather than work.

## Open in the FUZIX tree, not here

Kept in one place because both have been re-discovered more than once
and neither belongs to mmb2c:

* **The ~150 ms machine stall.** `pcmpace` is the scanner. Open since
  the PLAY SOUND work; the 186 ms cushion that shipped is a cushion, not
  a fix.
* **The console wedge.** Intermittent console death during transfers;
  not the filesystem, not a panic. `dnull.py` is the discriminator and
  the recovery recipe is written down. No kernel fix yet.

## Deliberately closed

`sort -k`, `diff -u` and `grep -E` are userland, not mmb2c, and are
recorded in the FUZIX tree. The `__mmb_main` wrapper and inlining
`MOD`/`\` by a literal are killed on measured evidence — see the "do not
re-propose" table in PLAN-emission-next. The kernel blit engine is
killed on a structural argument: the kernel cannot call program code,
because a bcrun function pointer is an offset into `code[]`.

`CSUB` and `INTERRUPT` are out together: `AllCommands.h` points
`Interrupt` at `cmd_csubinterrupt`, so it arms a CSUB as a handler and
has nothing to arm. `IRETURN` goes with them — it returns from a handler
written as a label or a line number, and `int_handler()` refuses both by
design, so a translated handler is a SUB and `END SUB` is its return.

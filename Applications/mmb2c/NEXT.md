# The queue

One ranked list. **Reviewed 2026-08-23 against the tree** — every entry
below was checked by reading the code or running it, not by reading the
notes. The review was a twelve-agent audit over the whole of mmb2c, the
kernel and the reference; what it found is recorded here and in
`COVERAGE-STATUS.md`.

Detail lives in the documents named in each entry; this file is only the
order and the reason.

**Language coverage is NOT ranked here.** `COVERAGE-STATUS.md` is
generated from MMBasic's `AllCommands.h` and mmb2c's own dispatch, so it
cannot go stale the way this file does. Take gaps from there; this file
carries only the work that a name-by-name list cannot express.

**And a warning about that claim, earned on 2026-08-23**: generated is
not the same as correct. The MATH section reported `C_ADD` .. `C_XOR` as
missing for as long as they had been shipping, because the scraper read
the body of `do_array_cmd` and their table sits three lines above it;
and `mkstatus.py`'s own documented command dropped the entire MATH
section, because it never called `mathstatus.py`. Both are fixed. A
generator only cannot flatter you if something checks the generator.

## Corrected on this pass — do not re-do these

Checked in the tree on 2026-08-23. Each names where to look.

| said | actually |
|---|---|
| "Split SUBs over the translated-size ceiling — **gone**" (2026-08-18) | **Wrong, and it is the only correction in that table that was itself wrong.** The ceiling is alive at `Applications/CC/backend-thumb.c:3861`, `t_bail = "size policy"`, with `t_maxfn()` at :260 capping a function at 40,000 bytes; a routine over it silently drops to bytecode. The 2026-08-18 review grepped mmb2c.py and mmbc/ — the ceiling lives in a third directory. |
| #2 `MATH CRC` **and** `BASE64` | `BASE64` shipped 2026-08-13 (`fcc7ff`); all four CRCs shipped 2026-08-23. The item is closed. |
| #5 "Neither interrupt phase-2 source exists yet" | PLAY-done existed already for `PLAY TONE` and `PLAY MODFILE` (`mmb_int.h:157-186`), and its code cites this file by name. MP3/WAV/FLAC completion shipped 2026-08-23. What is left of #5 is COM rx level, which is not a small poll — see below. |
| Steer: "the PIO block — 25 of 29 names in category 3, all or nothing" | Category 3 is four names now. The PIO steer was given on 2026-08-22 (a separate assembler) and the block is **four independently shippable pieces**, not one. |
| Steer: `JSON$`, `WATCHDOG`, `CPU` | All three shipped with the WEB campaign (v0.20). |
| Steer: `VAR SAVE` / `VAR RESTORE` | Decided the same day it was written: `catmap.py` has `"VAR": 4`, deliberately out. |
| "`cpptest.sh` is board-only by construction" | It builds `cpp` with host gcc and passes on the host, and has since 2026-08-08. The genuinely board-only gate is the c-testsuite — whose runner `ctb3.sh` is **not in the repository at all**. |
| The console wedge is open | A reproduction, a mechanism and a fix landed 2026-08-21 (`abe9b1b7f`, `NOTES-CORE0-STALL.md`), with one unexplained field observation to check on any recurrence. |

Every `mmb2c.py` line number the previous edition cited had drifted by
about a thousand lines. Cite, but re-check before trusting.

## Done since the last review

`SETPIN FIN/CIN/PER` (the counting inputs), `WS2812` and `BITSTREAM`
through fixed PIO1 programs, `OPTION ANGLE`, `MATH CRC8/12/16/32`,
`PLAY MP3/WAV/FLAC` completion interrupts — and the two silent
divergences below, which is the part worth remembering.

**2026-08-24: multi-dimensional array storage order.** Our C arrays were
declared in source order, so the LAST BASIC subscript was the adjacent
one — the transpose of MMBasic, which stores the FIRST adjacent
(`findvar`, `MMBasic.c:4871-4878`). The layout is not an implementation
detail, because a program reads it with `VARADDR`, so it is now
MMBasic's: the C dimensions are declared reversed and `subscript()`
indexes reversed. Both translators, five sites each, `tests/order.bas`
pins it.

Three things were computing wrong answers and now do not:

* `PEEK(VARADDR a(i,j))` gave the transposed offset — and for a
  non-square array the wrong stride as well.
* `READ a()` into a 2-D array filled the elements in our order, not
  MMBasic's: `DATA 1,2,3,4` into `a(1,1)` gave `1 3 2 4`.
* **`REDIM PRESERVE` on a multi-dimensional array corrupted it**, and
  that one was wrong on its own terms rather than merely different.
  `mm_arr_swap` enforces MMBasic's only-the-last-index rule and copies
  the old block's prefix, which preserves everything only when the last
  dimension's size multiplies no subscript — true of MMBasic's layout,
  false of the one we had. `REDIM PRESERVE p(n,2)` on a `p(1,1)` holding
  1,2,3,4 gave `1 4 3 0`.

`MATH(CRC…)` and `SORT` over a 2-D array, and comms tx/rx of one, were
divergent for the same reason and are not any more. `ARRAY`/`MATH SLICE`
and `INSERT` were **not** affected: `array_vector` reached the same
elements from the other end, and only its stride changed.

`tests/order.bas` pins all of it, and **all 14 of its lines are blessed
against a real MMBasic** — byte for byte, `devtools/ab.py` is the check
and `devtools/mmbrun.py` the transport.

Thirteen of them agreed on the first run. The fourteenth,
`REDIM PRESERVE`, did not — and the fault was the interpreter's:
MMBasic 6.03.02 preserved **nothing**, for 1-D, float and string arrays
as well, at every size including a PSRAM-resident one. It was fixed in
the reference and the two now agree. Ours had been wrong too, and
differently: `1 4 3 0`, scrambled with an element lost.

It cost nothing measurable: `make check` unchanged (nothing needed
re-blessing), `cgate.sh` back to 0, and on the board a 32x32 sweep runs
at 74 ms in either index order, before and after, at the same code size.
Arrays live in SRAM, which has no data cache, so the stride is free.

`linear_index()` in both translators is now a leftover — it decomposes a
flat position into a subscript list, and the flat position is the answer.
It can be replaced by a cast and an index whenever someone is in there.

**2026-08-24: the first batch of MATH additions.** `SHIFT`, `POWER`,
`V_NORMALISE`, `V_CROSS`, `V_PRINT`, `M_PRINT`, `MATH(MAGNITUDE)` and
`MATH(DOTPRODUCT)` — the eight members that needed no new machinery.
32 of 67 members to **40 of 67**. `tests/matha.bas` covers all eight,
values and error wordings alike, and every one of its 30 lines is
blessed against a real MMBasic with `devtools/ab.py`.

Three things the interpreter had to settle, none of them guessable from
the source:

* **`MATH SHIFT`'s fourth argument is a bare `U`.** Quoted, `"U"` is
  silently ignored and you get the ARITHMETIC shift — `checkstring`
  compares the argument's own text and the quotes are part of it. A
  wrong answer rather than an error there, so we refuse the quoted form
  instead of copying the silence.
* **`MATH POWER` into an integer array rounds the exponent first**, so
  `POWER a%(), 2.7, b%()` cubes. Into a float array it does not.
* The error wordings, which are not uniform: `SHIFT` raises
  `Size mismatch` and everything else `Array size mismatch`, because
  `cmd_math` uses `error()` for one and `StandardError(16)` for the
  rest. All of them came out of `MM.ERRMSG$` under `ON ERROR SKIP`,
  not out of the source.

`array_plane()` landed with `M_PRINT` and is what the remaining `M_*`
family needs; with the storage order now MMBasic's, those six are
transcriptions rather than rewrites.

**2026-08-24, later: OPTION BASE 1 arrays are dense.** Found while
adding the batch above, in code that had been shipping: every
whole-array walk folded in an unreachable element 0. `MATH(MAX)` over
an all-negative array answered **0** where MMBasic answers -5;
`MATH(MEAN)` divided by one too many; `MATH(MEDIAN)` took the median of
one element more than existed. `SUM` and `MIN` looked right only on the
data I first tried — `MIN` over positives is wrong the same way `MAX`
was.

The root cause was the array, not the reductions: `DIM a(3)` is FOUR
elements under BASE 0 and THREE under BASE 1, and we allocated four
either way. So the fix is where the cause is — `count_of()` allocates
what the program can reach, `rebase()` turns a subscript into an index
from element 0, and BASE 1 storage is now dense and identical to
MMBasic's, as BASE 0 already was.

That fixed a class, not six symptoms: `VARADDR` under BASE 1, `READ a()`,
`SORT`, `MATH(CRC…)`, comms tx/rx and the reductions all came right at
once. `tests/optbase1.bas` pins them, 19 lines blessed against the
interpreter.

**The one design choice worth knowing.** The hidden bounds table an
array parameter carries now holds the element COUNT LESS ONE, not the
upper bound the program wrote. Under BASE 0 they are the same number;
under BASE 1 they differ by one. Holding the count is what let
`mm_arr_count`, `emit_dim_alloc` and the subscript fold stay exactly as
they were — `BOUND()` is the single place that adds the base back, via
`unbase()`. Nothing a BASE 0 program generates changed by a character,
which is how the gates stayed readable through the change.

## Next

**2026-08-24, later still: the array size checks, and SPRITE LOADARRAY
in two dimensions.**

`ARRAY ADD` and `MATH SCALE` never checked that source and destination
were the same length — `mm_arr_add_i` and friends took one count and
never looked at the destination's, so a short destination was written
past the end in silence. Both check now, and **the two wordings differ
on purpose**: asked on a real MMBasic under `ON ERROR SKIP`, `ARRAY ADD`
raises `Array size mismatch` (`StandardError(16)`, in `array_add`) and
`MATH SCALE` raises `Size mismatch` (a bare `error()`, in `cmd_math`).

The same probe answered a question nobody had asked: **`ARRAY SCALE` is
"Unknown command" on a real MMBasic** — `SCALE` lives only in
`cmd_math`, and `cmd_array` has no such sub-command. We accepted it;
it is refused now, in our own words. The earlier entry in this file had
that wrong twice: it said `ARRAY SCALE` raised `Array size mismatch`,
when it does not exist at all, and the wording it quoted belongs to
`ADD`.

`SPRITE LOADARRAY` now takes a one- OR two-dimensional array. The 1-D
form is MMBasic's own and unchanged — `w * h` pixels in sequence. The
2-D form is ours: MMBasic refuses it with "Argument 4 must be a 1D
numerical array", and it costs no code here because the first BASIC
subscript is the adjacent one, so `DIM s(w-1, h-1)` walked flat IS the
raster, row by row, with `s(x, y)` the pixel at x, y. Three or more
dimensions are refused. `tests/arrsize.bas` covers the size checks
(blessed against the interpreter); `tests/sprite.bas` covers the 2-D
load and its `Array Dimensions` boundary headless.

### 1. Nothing else may swallow a statement

Two shipped features were computing wrong answers in silence, and the
gates could not see either: `OPTION ANGLE DEGREES` translated with rc=0
and then worked in radians, and an untranslated `MM.` read became an
implied variable worth zero. Both are closed. What is NOT closed is the
class:

* **Both translators exit 0 when lines were commented out.** The message
  goes to stdout and nothing else. `--strict` exists and returns 2, but
  no gate passes it — `cgate.sh`, the Makefile and `fcctests.sh` all
  invoke the translators bare. A shipped sample, `samples/robots.bas`,
  drops eleven lines today and nothing says so.
* Decide the default: either a dropped line is a non-zero exit unless
  asked otherwise, or every gate passes `--strict` and carries a small
  allowlist. The second is a morning's work; the first is a policy
  change worth making deliberately.

### 2. `MATH RANDOMIZE` seeds the wrong generator

`MATH RANDOMIZE n` here seeds the generator `RND` draws from
(`mmb_runtime.c:820-843`). In MMBasic it seeds a Mersenne twister that
**only `MATH(RAND)` consumes**, while `RND` is libc `rand()` reseeded
from hardware; and the standalone `RANDOMIZE` command does not exist on
rp2350 at all (`Commands.c:5474`). So a program that says `MATH
RANDOMIZE 42` gets a reproducible `RND` here and a random one there.
Small, and a decision rather than work: match the reference (and leave
`RND` unseedable), or keep ours and write it down.

### 3. Pixel batching phase 2: the deferred tail

`MM_PIX_LATENCY_US` is 10 ms but the bound is only tested inside
`mm_pixel`, so a program that plots and then computes leaves its last
partial batch unpainted. **It is a PROCESS alarm, not a hardware one** —
`p_alarm` is decremented in the existing decisecond pass
(`Kernel/process.c:487-489`), so the full hardware alarm pool is
irrelevant and 100 ms is the natural granularity. Two things the plan
did not know: the busy flag is provably necessary because
`preempt_handler()` dispatches signals at arbitrary user PCs, and there
is an EINTR hazard — Fuzix's `fread` never retries and permanently
error-flags the stream, so the alarm must be DISARMED at drain rather
than left to fire harmlessly.

**And a second hole in the same item, unrecorded until now**: a program
whose `main()` simply returns never flushes at all — only
`mm_raw_release` is registered with `atexit` — so the last batch is lost
at exit even with no compute tail.

### 4. The cheap coverage wins, in this order

Each is small, self-contained, and needs no decision:

* **`SYNC`** — 47 lines of arithmetic over `time_us_64` in the
  reference, and `pc3_us64()` plus `mmb_wait.h`'s deadline loop are both
  here. A frame metronome is what every translated game wants.
* **`IR SEND`** — a list of edge durations, which is exactly what the
  shipped `BITSTREAM` PIO path already takes. No kernel work at all,
  and better-than-reference timing; the only cost is PIO1's pin window
  (GP0–GP7, GP26). The measurement that reshaped this family is in
  PLAN-pulsin.md: a busy-wait cannot be trusted here, so anything that
  can be handed to hardware should be.
* **the vector/matrix members** — and this got cheaper. Four of them
  never needed a 2-D accessor at all: `MAGNITUDE`, `DOTPRODUCT`,
  `V_NORMALISE` and `V_CROSS` call `parsefloatarray(..., 1, ...)` in
  the reference, so they are 1-D float loops, about 60 lines of header
  between them. The rest — `V_MULT`, `V_ROTATE`, `M_TRANSPOSE`,
  `M_MULT`, `M_INVERSE`, `M_DETERMINANT` — walk a 2-D array flat, and
  since the storage-order change our flat order IS MMBasic's, so they
  are straight transcriptions rather than strided rewrites. No
  `array_matrix()` helper is needed. `tests/solar_eclipse.bas:3068-3130`
  hand-writes `matxvec` and `transpose` over 3x3 arrays, which is the
  only demand evidence in the tree.
  **Watch the convention**: MMBasic's `farr2d(arr,d1,a,b) = arr[b*d1+a]`
  means subscript 0 is the COLUMN, the transpose of how the eclipse
  writes it. A program "simplified" to use `MATH V_MULT` without
  transposing would silently compute the wrong thing — and that is a
  fact about MMBasic, not about our layout, so it survives the change.
* **`MEMORY COPY` and `MEMORY SET`** — no new libcall and no kernel
  change: `memcpy`, `memmove` and `memset` are already in bcrun's table
  and `memcpy` has a native fast slot. But note what the old entry got
  wrong: the framebuffer is NOT addressable from userland, so "a program
  driving its own display" is not the use. Array-to-array,
  array-to-LONGSTRING and reading kernel font glyphs are.
* **`GetScanLine`** — a transcription, not a decision: the counter
  exists (`display.c:530`), is maintained per line on core1, and our two
  rasters' blanking/total constants (45/525 and 38/806) are MMBasic's
  `fun_getscanline` constants verbatim.

### 5. The rest of the timing family, now the measurement exists

**The probe was run and `Pulsin(`/`Distance(` shipped on it**
(PLAN-pulsin.md). The number: a userland spin here loses 14–18 µs about
345 times a second to the tick, and **half a second** whenever a second
process is runnable. So the question for the rest of the family is no
longer "does MMBasic mask interrupts" but "what measures this without
a spin":

* **`IR SEND`** — nothing to decide, see item 4: it is a list of edge
  durations and the shipped `BITSTREAM` PIO path already takes exactly
  that.
* **`OneShot` and IR RECEIVE** — GPIO edge interrupts with their own
  timers in the reference, so they want the edge CAPTURE that
  `Pulsin(` now runs on (`countpin.c`, `GPIOC_CNT_CAP`), not a loop.
  IR receive additionally needs the 1 ms assembly timer MMBasic runs
  its state machine on.
* **`Humid`** — the one that can still be an honest busy-wait, because
  a DHT frame carries a checksum: a corrupted read is detectable and
  retryable, which is precisely why the reference gets away with it.

### 6. PIO, in four pieces

The steer was given: a separate assembler, and mmbc owes only the
runtime surface. The audit costed it, and it is better than it looked.

1. **`mmpioasm`** — MMBasic's assembler is not woven into the
   interpreter: the 22 in-language statements are 90 lines of string
   concatenation (`Custom.c:2847-2977`) feeding one 921-line
   `checkstring` case whose entire dependency on MMBasic is `getint`,
   `GetMemory`, `error` and one hardware store. Lift it, replace those,
   and it is a ~1300-line standalone program our own `cc` compiles, that
   accepts MMBasic's PIO source text VERBATIM. Prove it by diffing its
   32 output words against a real PicoMite's `PIO ... LIST`. Do this
   first: it is independent of every kernel question, and until it
   exists the runtime surface has nothing to load.
2. **The kernel's PIO0 arbitration.** Nothing claims PIO0 (verified by
   grep — only `sound.c:1128` and `pioout.c` touch pio1), and PIO0's
   GPIOBASE is NOT pinned, so `PIO SET BASE 0,16` reaches GP32 and
   GP34-GP46 — thirteen header pins no PIO on this machine can currently
   drive. Instruction memory is a whole-block resource (`PIO PROGRAM`
   always loads 32 slots), so PIO0 must be claimed as a block, not per
   state machine. One latent bug to fix while there: `pinlock.c:202`
   calls `pioout_sm_reset()` for every `PLK_PIO` index, and that
   hard-codes pio1's machine.
3. **The runtime surface**, no DMA: load, configure, start, stop, FIFO
   in and out, `Pio(`. One care: `PIO CONFIGURE` enables the input
   buffer on every pin 1..43 as its last act (`Custom.c:2537`), which
   here would reach the SD card, the console UART, the DS3231 and the
   display. Scope it to owned pins and write the divergence down.
4. **The DMA forms**, which is the only part with an open design
   question — and a smaller one than the DMA law suggests:
   `PSRAMIOC_ALLOC` already hands userland a never-moved, never-swapped,
   exit-released buffer. What is missing is that no translated program
   can reach the arena at all: a new libcall name, not a new mechanism.

### 7. One release checklist that names every gate

`relcheck.sh` checks the release number in three places and nothing
checks that the gates were run. They all exist and all exit non-zero:
`make check` and `mmbc/cgate.sh` here, `ioctlcheck.sh`, `mancheck.sh`,
`usbcheck.sh`, `kbdsync.sh`, `relcheck.sh` and `Applications/cpp/cpptest.sh`
in the platform tree, `fcctests.sh` and `qemutests.sh` in CC. The one
that cannot be automated is the c-testsuite at `/root/ct` on a booted
board — and its runner is not in the repository, which is the first
thing to fix.

## Wants your steer before any work

* **`FFT`** — **the design question is gone.** Its complex forms want
  MMBasic storing the first subscript adjacent in a 2-D array, and that
  is now our layout too, so what is left is size, not a decision.
  `MAGNITUDE` and `PHASE` remain a clean cheap subset.
* **`ADC`** — continuous sampling, and the only category-2 name blocked
  by the machine rather than by effort: MMBasic DMAs the ADC FIFO
  straight into the BASIC array, which the DMA law forbids here, and the
  kernel has no ADC path at all today. A real kernel driver with its own
  .bss double buffer.
* **`TILE`** — 69 reference lines over a structure the kernel already
  maintains, but our console writes the cell colours on every character
  where MMBasic's `PRINT` leaves them alone. Needs a "hold" flag so a
  program using TILE keeps MMBasic's model without costing the shell its
  ANSI colour. Settle the reference's behaviour side by side first.
* **COM rx level** — NOT a poll. `OPEN comspec$ AS #n` is refused in
  both translators and `LOC()` is `ftell`, so the interrupt has nothing
  to attach to: the real item is the serial-port family. The kernel half
  is free (`TIOCINQ`), and one constraint to respect — Fuzix's tty queue
  is 256 bytes where MMBasic's buffer defaults to 1024, so a level above
  256 must be refused rather than silently accepted.
* **`RESOLUTION`, `REFRESH`, `MEMORY`** — the rest of category 3.
  `RESOLUTION` looks undeliverable (clk_sys is fixed at boot by one call
  that must stay one call, and here the MODE picks the raster);
  `REFRESH` has no target but a no-op is exactly what MMBasic does with
  `AUTOREFRESH ON`, so it is a ten-line compatibility shim the day a
  ported program needs it.
* **Graphics: `Turtle` and `Draw3D`** — wanted eventually, **decided
  2026-08-23 to be not imminent**, and in category 3 rather than 2 so
  that "somebody could pick this up tomorrow" keeps meaning something.
  Turtle is pen state over primitives that already exist; Draw3D is
  transform, projection and an object table of its own (~1,850
  reference lines).

  **`Mandelbrot`, `Ray` and `Frame` are NEVER** — decided the same day
  by MMBasic's own author: Mandelbrot is a silly easter egg there, and
  the other two are not wanted here. They are category 4 now, recorded
  as decisions rather than gaps, so no future review proposes them
  again. Do not resurrect them from an old plan document.

  `TILE` and `Tilemap` stay in category 2 and are unaffected.

## Open in the FUZIX tree, not here

* **The ~150 ms machine stall.** `utils/pcmpace.c` is the scanner.
  Nothing has moved since 2026-08-13; the 186 ms cushion that shipped is
  a cushion, not a fix.
* **The console wedge** — mechanism found and fixed 2026-08-21, with one
  field observation (phase=0 where the reproduction says phase=2) to
  check if it ever recurs.

## Deliberately closed

`sort -k`, `diff -u` and `grep -E` are userland, not mmb2c. The
`__mmb_main` wrapper and inlining `MOD`/`\` by a literal are killed on
measured evidence — see the "do not re-propose" table in
PLAN-emission-next. The kernel blit engine is killed structurally: the
kernel cannot call program code, because a bcrun function pointer is an
offset into `code[]`. `CSUB`, `INTERRUPT` and `IRETURN` are out
together. **`int narrowing of loop counters` joins them**: the win it
aimed at was banked when cc2 inlined the 8-byte compound assigns, and
the adjacent 64-bit register-cache experiment was built, measured and
shipped OFF at -0.3%.

`mm_mark`/`mm_release` elision is NOT closed but is smaller than it
reads: every routine opens with `unsigned __mark = mm_mark();` and exits
with `mm_release(__mark)` on all nine exit paths, whether or not it
takes a string temporary — so it is TWO crossings per call, not one, and
`bcrun`'s per-libcall profiler under `BCRUN_PROF` can size the win
before a line of it is written.

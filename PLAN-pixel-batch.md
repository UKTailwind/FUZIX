# PLAN-pixel-batch: batching scalar PIXEL through GFXIOC_PIXELS

Status: **BUILT and board-verified.**  Designed 2026-08-09, shipped in
v0.9; this header said "not implemented" until 2026-08-12, which was
simply never updated after the work landed.

What is in the tree: the accumulator (`mm_ptbuf`/`mm_pixn`,
mmb_runtime.c:4371), `mm_pix_drain()` at 4401, and a `mm_pix_drain()`
call at the head of every primitive that must not be reordered around
queued pixels - the list below is the one that was implemented.
Measured: ripple -21% against v0.9.

The rest of this document is the design as agreed, kept because the
ordering argument in it is the reason the drain calls are where they
are, and anyone adding a new primitive needs it.

## The problem, in numbers (all board-measured)

A scalar `PIXEL x,y,c` statement today is `mm_pixel()`
(mmb_runtime.c:3671): a cached colour push plus one `GFXIOC_PIXEL`
ioctl, ~1.7us per statement.  The breakdown, from syscallbench and
PC3-GFX-DESIGN.md:

    getpid()                     597 ns   the trap floor
    ioctl, packed argument      1300 ns   GFXIOC_PIXEL as measured
    the pixel store itself     15-50 ns

So ~96% of the cost is the crossing, and no optimisation inside the
kernel handler can recover more than ~2x (the 597ns floor).  The
kernel already has the answer: `GFXIOC_PIXELS` draws a whole run in
one crossing, reads the arrays where they lie, takes a per-item
colour array with last-value caching, and the runtime already uses it
for the array form and for lines.  512 points per crossing amortises
the 1.3us to ~2.5ns per pixel.

Target: scalar PIXEL under 150ns amortised, ~10-20x.  And the win
grows as cc-perf work lands: "the faster the compiled code gets, the
more the syscall overhead matters" (PC3-GFX-DESIGN.md).

## Constraints, in priority order

1. **Observable behaviour identical to MMBasic.**  Read-back,
   drawing order, palette timing, and visibility within about a
   frame.  A silent divergence outranks any speedup (triage rule:
   different is worse than missing).
2. **No kernel changes.**  Existing ioctls only.
3. **Degrade to the status quo, never to wrong.**  Any environment
   where the design's assumptions do not hold (host build, qemu
   gates, unknown board) disables batching and becomes exactly
   today's one-ioctl-per-pixel path.
4. **Board-verified before claimed.**  See Verification.

## What already exists (facts, with line numbers as of today)

- `mm_pixel` (mmb_runtime.c:3671): `mm_gfx_setcol(rgb)` then
  `GFXIOC_PIXEL` with `MM_GFX_PACK(x, y)`.
- `mm_ptbuf[MM_BATCH]`, `MM_BATCH` = 512, and `mm_gfx_flush_pts()`
  (mmb_runtime.c:3762-3775): the batch machinery, currently used
  transiently by `mm_line` and friends with `colours = NULL`.
- The graphics-text run queue `mm_gbuf`/`mm_gn` with `mm_gflush()`
  (mmb_runtime.c:3409): the precedent for a queue that persists
  ACROSS statements - a `PRINT "abc";` leaves glyphs buffered until
  newline, tab, a full buffer, or an explicit flush.
- `mm_end` (mmb_runtime.c:2639) already calls `mm_gflush()` before
  exit.
- `mm_us_now()` is `time_us64()`, a libcall - a SYSCALL.  It cannot
  be used per-append.
- Kernel side: `display_gfx_pixels` (display.c:1331) clips each
  point against the live geometry and caches the colour mapping
  against the last value, so a constant-colour run maps once.  It
  does NOT touch the kernel's current colour, so the runtime's
  `mm_gfx_col` cache stays valid across a drain.

### A bug this fixes for free

`MM_GFX_PACK` masks to 10+9 bits, so today `PIXEL 1030,100` WRAPS to
x=6 and draws there.  MMBasic drops off-screen pixels.  The batch
path carries signed int16 and the kernel clips properly, so the
accumulator makes out-of-range PIXEL behave like MMBasic.  This is a
deliberate behaviour change in MMBasic's direction; cover it in the
side-by-side test.

## Design

### The queue

Reuse `mm_ptbuf` and add:

    static unsigned long mm_colq[MM_BATCH];  /* RGB888 per point   */
    static int mm_pixn;                      /* queued count       */
    static unsigned long mm_pixt0;           /* us at first append */

+2K of static data (the colour array).  Reusing `mm_ptbuf` is safe
because every other consumer of it must drain the queue first anyway
(ordering demands it - see the uniform rule), so sharing the buffer
adds no constraint that ordering had not already imposed.

### Append: mm_pixel becomes

1. `mm_gfx_open()` as today; return silently if no display.
2. If a text run is pending (`mm_gn > 0`), `mm_gflush()`.  **The two
   queues are never non-empty together** - see Ordering below.
3. Resolve the colour NOW: `rgb == MM_CUR` becomes `mm_gfx_fg` at
   append time.  COLOUR may change before the drain; MMBasic reads
   the colour at statement time, so the queue must too.
4. Coordinate guard: drop the point if x or y is outside
   [-32768, 32767].  The kernel clips everything inside that range;
   the guard only prevents short-cast wraparound re-aliasing a far
   off-screen point back onto the screen.
5. Append point and colour.  If the queue was empty and the fast
   clock is available, record `mm_pixt0`.
6. Drain if `mm_pixn == MM_BATCH`, or if the fast clock says more
   than `MM_PIX_LATENCY_US` (10000) has passed since `mm_pixt0`.
   The elapsed check is one volatile load, an unsigned subtract and
   a compare - per append, no sampling tricks.
7. If the fast clock is NOT available, drain immediately - queue
   depth 1, behaviour identical to today.

### Drain: mm_pix_drain()

If `mm_pixn == 0`, return.  One `GFXIOC_PIXELS` with
`b.colours = mm_colq`, then `mm_pixn = 0`.  Errors are ignored as
they are today (a program with no display draws nothing).

### The uniform rule

**No graphics ioctl is issued while the pixel queue is non-empty.**
Mechanically: `mm_pix_drain()` is the first statement of every
function in the graphics section that reaches
`ioctl(mm_gfx_fd, ...)`.  When the queue is empty it is one compare.
Reasoning per call site is how an ordering bug slips in; the uniform
rule is cheaper than being clever.  The sites (from today's grep):

| Site | Hazard if not drained first |
|---|---|
| `mm_pixel_get` | read-after-write: `PIXEL x,y,c : IF PIXEL(x,y)<>c` must see c.  Drain and delegate; never answer from the queue - the kernel owns colour reduction and clipping, and duplicating either is a silent-divergence factory |
| `mm_gflush`, the scroll in `mm_gputc` | text must overdraw earlier pixels; SCROLL must move pixels that have landed, and queued pixels would otherwise land un-scrolled afterwards |
| `mm_gfx_rect`, `mm_plot`, `mm_fill`, `mm_line`, `mm_pixels` | draw order; these also reuse `mm_ptbuf` |
| `mm_mode` | queued pixels land in the OLD mode before the switch |
| `mm_map`, `mm_map_set`, `mm_map_reset` | palette collection/application order |
| `mm_fb_open/sel/copy/wait/paint/clear_hw` | FBSEL: queued pixels must land in the target that was selected when they were queued.  FBCOPY is a snapshot.  VSYNC: a program that waits for blanking expects its drawing to be there |
| `mm_hres`, `mm_vres`, `mm_fontinfo`, `mm_at` | no true hazard (read-only geometry), but exempting them means maintaining an exemption list; drain costs a compare |
| `mm_end` | drain before exit, next to the existing `mm_gflush()` |

Also confirm `mm_error` exits through `mm_end` (or add both flushes
to its path): an errored program's picture stays on screen in
MMBasic.

### Ordering between the two queues

The text queue persists across statements, so PIXEL and PRINT can
interleave.  The invariant that keeps program order without
reasoning about interleavings: **at most one queue is non-empty at
any moment**.  Enforced in both directions -

- `mm_pixel` flushes the text run before appending (append step 2);
- `mm_gputc` drains the pixel queue before buffering a character.

With that, every drain lands work in exactly program order.
(`mm_gflush` mid-line is already a supported operation - tab does
it - so flushing a partial run costs nothing new.)

### The fast clock

The 10ms latency bound needs a time source costing nanoseconds;
`mm_us_now()` is a syscall and would eat the entire win.

On a real PC3 - and only there - read the RP2350's free-running
microsecond counter directly: `TIMER0` `TIMERAWL`, one volatile
32-bit load.  Verify the address against the RP2350 datasheet before
use (TIMER0 base 0x400B0000, TIMERAWL at +0x28 = 0x400B0028).  The
32-bit wrap (~71.6 min) is harmless for a 10ms comparison done with
unsigned subtraction.  This is the established no-MMU pragmatism -
same class as the PICOIOC_LIBM table and ADVAL(-9) - and it is a
read-only register touching no kernel state.

Gate it on the board answer the runtime already fetches
(`MM_PICOIOC_BOARD`, used by `mm_device`): cache it at
`mm_gfx_open`; enable the direct read only for boards 2 and 3.
Everywhere else - host build, qemu gates, an unknown future board -
the clock is "unavailable" and append step 7 keeps the queue at
depth 1.  The gates therefore exercise the identical code path,
just without batching.

Rejected alternative: count-only flushing with no clock.  A program
that plots a handful of pixels and then computes for seconds makes
NO runtime calls in the gap (mmbc compiles loops to native code -
the eclipse pattern), so the tail would sit unflushed for the whole
compute.  That is a visible divergence, which rules it out.

### Why a 10ms deferral is observationally MMBasic-exact

The scanout refreshes at ~58-60Hz; nothing a program draws can
appear before the raster next passes that line.  A <=10ms flush
bound adds at most one frame to that.  And VSYNC drains first, so
code synchronised with FRAMEBUFFER WAIT sees exactly what MMBasic
would show.

### Phase 2 — TO DO: the safety net is 100ms, not 1s

**Implemented and shipped: everything above.**  Board: ripple 193.55 ->
171.46 ms (-11.4%, and -21% against v0.9's 217.43), sombrero 2569.7 ->
2454.8 (-4.5%), bubble with its hand-batching removed 117.60 -> 95.42
(-18.9%), bubble as written unchanged at 74.0.  The same picture saved
under both runtimes is byte-identical - which is how the one bug was
found: SAVE IMAGE forks a program that reads the framebuffer, a
crossing the uniform rule does not cover, so the last partial batch was
missing from the file.  mm_run_exec flushes before it forks now.

**What is left is the tail case**: the 10ms bound only advances while
the program keeps calling mm_pixel, so plot-a-few-then-compute-silently
can defer them for the length of the compute.

The plan below said 1s because that is what alarm() offers.  **It is
actually 100ms**: alarm() is the POSIX seconds wrapper, but the raw
syscall takes DECISECONDS - `_alarm(dsecs)` in syscall_proc.c - and
process.c decrements it once per decisecond tick.  So `_alarm(1)` is a
100ms backstop, six frames rather than sixty.  (proc.h calls p_alarm
centiseconds; the code says otherwise.)

A process cannot see the tick any other way: it either asks, which is
the syscall this exists to avoid, or it is signalled.

Still worth keeping separate from phase 1: it is the only signal path
in the runtime, the handler must not land between the append and the
count update (the busy flag below), and it wants its own test - a
program that plots and then computes in silence - plus a check that
nothing else wants alarm() (PAUSE uses usleep, SETTICK is not
implemented, so it looks free).

### The design, as agreed

The 10ms bound only advances while the program keeps calling
`mm_pixel`; the plot-then-silent-compute pattern can still defer the
tail for the length of the compute.  If side-by-side testing shows
this matters in practice: `alarm(1)` while the queue is non-empty,
a SIGALRM handler that drains under a busy flag (incremented around
append and drain; a handler that sees it set returns and the next
append catches up).  Worst case becomes 1s (Fuzix alarm granularity).
Keep it out of Phase 1: signal re-entrancy against ioctl deserves
its own test pass, and one change at a time.

## Out of scope - considered and rejected (review of 2026-08-09)

- **Kernel if-chain reorder / switch**: <=30ns of 1300.
- **A dedicated syscall**: capped at ~2x by the 597ns trap floor,
  and it diverges from the core Fuzix syscall table that is being
  upstreamed.
- **A direct-call pixel entry via the libm table, or a published
  framebuffer pointer**: the non-preemptive kernel is what makes
  `display_fb_enter` + draw atomic; a preemptible userland call can
  be descheduled between repoint and store and land in another
  process's layer.  PC3-GFX-DESIGN.md already decided "no raw
  framebuffer address is published"; the function veneer has the
  same hole.
- **A kernel-drained command ring**: saves nothing over a 2.5ns
  amortised crossing and puts drawing into interrupt context, against
  the scanout-contention and RTC lessons.

## Verification (the board is the authority)

1. **Correctness gate, on the board**: a BASIC program interleaving
   PIXEL writes with PIXEL() read-backs - same and different
   coordinates, per-pixel colour changes, MM_CUR with COLOUR changed
   mid-stream, out-of-range coordinates (the aliasing fix), MODE
   switch, layer select/copy, scroll, MAP/MAP SET.  Assert every
   read-back.  Run the identical program on a real PicoMite
   side-by-side; the side-by-side is the authority.
2. **Ordering**: PIXEL then LINE/BOX overdrawing it, then read-back;
   PIXEL then FBCOPY both directions; PIXEL then PRINT over the same
   cells and PRINT-then-PIXEL the other way; PIXEL then SCROLL.
3. **Performance**: the 1000-`PIXEL` loop (MMBasic 5us/statement,
   today ~1.7us, target <0.3us), and a pixel-bound bench
   (plasma/ripple-style .bas), A/B interleaved from fresh boots per
   the benchmark method notes.  Confirm the board runs the build you
   think it runs.
4. **Host gates unchanged**: fcctests/qemutests stay green with the
   accumulator disabled by the board gate (same code path, depth 1).
5. **No colour-cache regression**: RECT in the current colour after
   a batched run with per-item colours - the kernel's current colour
   must be what `mm_gfx_col` believes.

## Cost

+2K static (`mm_colq`), roughly 40 lines in mmb_runtime.c, one new
constant to verify against the datasheet, no kernel change, no
change to compiled programs or the mmb_gfx.h geometry headers.

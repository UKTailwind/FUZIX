# PLAN-interrupts: MMBasic software interrupts on the PC3 Fuzix port

Status: **BUILT and board-verified** (2026-08-10) - pins, SETTICK and
ON KEY, the whole of phase 1.  Translator + runtime work; no kernel
changes were needed, as predicted.

ON KEY: both forms, specific checked before any-key, the selected key
consumed and every other key left for INKEY$.  Board-verified at a real
console - "A" fired the specific handler and never reached INKEY$, "B"
and "C" reached it.  The decimation is here as designed (5ms), and the
decoded-key FIFO in front of mm_kq is what keeps the two forms' promises
apart.

**A DEFECT, found by testing rather than by reading, and it is not this
facility's**: INKEY$ HAS NEVER SEEN A KEYSTROKE AT THE PC3 CONSOLE.  ON
KEY inherits it and only fires when Enter completes a line, at which
point the whole line fires one handler call per character.

Diagnosed properly rather than guessed at, because the first two guesses
were both wrong.  It is NOT the kernel's canonical line discipline:
tty_inproc does insq() on every character and only withholds the WAKEUP
in canonical mode, while tty_read's remq() takes anything queued before
it ever sleeps.  It is NOT a flush on the mode change: libc's tcsetattr
maps TCSANOW to TCSETS, and only TCSETSF clears the queue.

It is `lineedit.c` - the PC3's own console line editor, which gives the
shell its history and cursor keys.  `lineedit_input()` swallows every
keystroke while the tty is in `ICANON|ECHO`, buffers it, echoes it
itself, and hands the finished line to tty_inproc only at Enter
(lineedit.c:302-304, 319).  mm_inkey flips to raw for the few
microseconds of ONE read and flips straight back, so the window is open
only during the read and every key is typed outside it.

Board proof, `utils/keyprobe.c`: isatty 1, tcgetattr ok, tcsetattr ok
and the raw mode READS BACK as set (ICANON off, VMIN 0) - and then
read() returns nothing across ten seconds of typing.  Every layer
reports success and no byte arrives.

**FIXED, 2026-08-10.**  The runtime takes the terminal on the first
INKEY$ or armed ON KEY and KEEPS it (mm_raw_hold), gives it back for the
duration of an INPUT - which wants the editor, the echo and the
backspace handling - and restores it at exit.  That is what the line
editor's own comment says raw-mode programs do, and what BBC BASIC and
the editors here already did.

Board-verified: a whole sentence typed on the USB keyboard and every
character read by INKEY$ as it was pressed; then ON KEY delivering keys
at the console with a SETTICK heartbeat running beside it.

Two things that cost a cycle each and are worth keeping:

  - the restore has to go through atexit(), not just mm_end().  A
    translated program's main ends with a plain return, so mm_end is not
    on the path - and a terminal left raw with VMIN=0 hands the shell an
    instant EOF, which it takes for a hangup and LOGS THE USER OUT.
    That is what it did.
  - with ECHO off there is no evidence a key was ever pressed, so "read
    nothing" and "nobody typed" look identical.  keyprobe.c clears
    ICANON but LEAVES ECHO ON for exactly that reason: the echo proves
    the key arrived, so a run that reads nothing is saying something.

One implementation trap worth keeping: mm_key_peek calls mm_inkey, which
returns a STRING and therefore costs a scratch slot - and the poll calls
it every 5ms for the life of the program.  Without winding the scratch
back it empties the pool and the program dies with "String expression
too complex" while containing no string expression at all.  The fcc gate
caught it; the host, with a larger pool, did not.

SETTICK holds a microsecond deadline off the fast clock rather than a
millisecond counter, exactly as designed below.  Board: 20 x 100ms in
1999.981ms, and a 10ms timer beside a 50ms one firing exactly 100 times
in the second the slow one needed for twenty.  PAUSE freezes the
time-to-go, RESUME rebuilds the deadline from it.

**One divergence, named:** MMBasic fires when its millisecond count is
*greater than* the period, so `SETTICK 100` runs at 101ms there and
`SETTICK 16` - the 60fps case its own game guide suggests - at 17ms,
about 6% slow.  A deadline has no such rounding, so this fires at the
period asked for.  Replicating the extra millisecond would be
replicating an artefact of the counter rather than a decision.

The clock: `pc3_us64()` in `<sys/pc3io.h>` (TIMERAWH/TIMERAWL with the
high/low/high loop), with `mm_us()` from the runtime as the fallback the
host and the gates use - so `tests/settick.bas` counts real ticks under
`make check`, `fcctests` and qemu rather than merely compiling.

What shipped: `SETPIN pin, INTH|INTL|INTB, handler`, the per-statement
poll gated on `uses_interrupts`, the no-nesting gate, one dispatch per
boundary, and the error-state save/clear/restore.  Runtime in
`mmb_int.h` plus `mm_int_err_push`/`pop` in `mmb_runtime.c` (and two new
bcrun natives).  Gates: make check 24, cgate 0 diff lines, fcctests 24,
qemu 25; `tests/pinint.bas` is the new case.  Board: joystick on GP35,
twelve INTB edges, none on the other three armed pins.

Three corrections to what is written below:

* **IRETURN is not implemented and does not need to be.**  This document
  says END SUB and IRETURN do the same thing inside a handler, which is
  true, but it is worth being sharper about why: for a SUB target
  MMBasic *builds* an IRETURN and uses it as the return address of the
  GOSUB it fakes (MM_Misc.c:10205-10210).  Written IRETURN exists for
  the label and line-number targets only - and those are refused here -
  so a handler is just a SUB ending in END SUB.
* **MAXINTERRUPTS is 10**, not the "N" left open below; matched.
* **`SETPIN 32, INTL, AlarmSub` for the DS3231 alarm now works** - but
  it did not when this was written, and the fix was not just widening a
  mask.  GP32 was outside the claimable set; it has been added, ON A
  PC3 ONLY, because the same kernel runs the Pico Computer 2 where GP32
  is the SD card's MISO.  Handing it out there would let a BASIC program
  take the card's data line and reset it on exit.  `board_is_pc2()`
  decides at claim time.

  Board-verified as `SETPIN 32, DIN, PULLUP` reading 1, the open-drain
  line idling high.  But **nothing can arm the alarm yet**: setting one
  means writing the DS3231's registers, and `/dev/i2c` refuses 0x68 to
  keep a program from stopping the oscillator on a battery-backed part.
  So the pin reads and can interrupt, and there is nothing to make it
  fire.  Closing that needs a kernel call to set an alarm - phase 2,
  and a smaller job than it looks now the pin half is done.

Original design proposal follows.  2026-08-10.

Covers SETTICK, ON KEY (both forms) and SETPIN INTH/INTL/INTB, with
IRETURN — the facility COVERAGE.md lists under "Interrupts and
background timing".  Source read: PicoMiteV6.03.00, core/MM_Misc.c
(with the arming commands in core/Commands.c and misc/External.c and
the tick feed in PicoMite.c).

## What MMBasic actually does (MM_Misc.c, V6.03.00)

The load-bearing fact: MMBasic "interrupts" are not interrupts.  The
whole facility is a poll, and no BASIC ever executes asynchronously.

- Hardware ISRs only set flags and counters.  The 1ms systick
  increments `TickTimer[i]` for each active tick (PicoMite.c:2311);
  the console ISR sets `Keycomplete` when the selected key arrives
  (PicoMite.c:934).  Nothing else happens at interrupt time.
- The interpreter calls `check_interrupt()` after EVERY statement
  (MMBasic.c:1878, suppressed by OPTION NOCHECK).  So the latency
  guarantee is one statement, and a statement is atomic.
- `check_interrupt` (MM_Misc.c:10217) is two cheap gates then the
  real scan: quick exit unless `InterruptUsed`; skip while
  `InterruptReturn != NULL` (already inside a handler — interrupts
  NEVER nest) or in immediate mode.
- `checkdetailinterrupts` (MM_Misc.c:9878) tests every armed source
  in one fixed order, and that order IS the priority scheme: PID,
  specific-key ON KEY, any-key ON KEY, PS2, PIO FIFOs, DMA done, GUI
  touch, sprite collisions, nunchuk, ADC done, I2C slave, WAV done,
  COM rx level, IR, keypad, CSUB, the pin table, ticks last.  First
  hit wins and ONE interrupt is dispatched per check; the next
  statement boundary picks up the next one.
- Pin interrupts are themselves polls (MM_Misc.c:10153): the current
  level against the level at the previous check.  INTH = fires on
  low-to-high (`v > last`), INTL on high-to-low, INTB on any change
  (keyword mapping External.c:1610-1619, 2053-2061; `last` is seeded
  from the pin at SETPIN time, External.c:2050).  There is no GPIO
  IRQ anywhere, and a pulse shorter than a statement is missed.
- Ticks fire when `TickTimer > TickPeriod` (strictly), and the
  catch-up loop DROPS missed periods rather than queueing them
  (MM_Misc.c:10176).  Four timers (NBRSETTICKS, configuration.h:409),
  ids 1-4, period in ms up to INT_MAX (cmd_settick, MM_Misc.c:2334).
  SETTICK PAUSE/RESUME freezes the count where it stands
  (`TickActive` gates the increment, PicoMite.c:2315).
- Dispatch (`GotAnInterrupt`, MM_Misc.c:10188): saves and CLEARS
  MMerrno, the error message and OPTION ERROR SKIP, records
  `InterruptReturn = nextstmt`, and for a SUB target fakes a GOSUB
  whose return address is a synthetic two-token IRETURN (`rti[]`,
  MM_Misc.c:10205) — which is why END SUB and IRETURN do the same
  thing inside a handler.
- `cmd_ireturn` (MM_Misc.c:1846): "Not in interrupt" if there is no
  saved return; otherwise jump back, restore, drop locals.
- Long statements cooperate rather than block: `cmd_pause`
  (MM_Misc.c:640) polls `check_interrupt()` in its wait loop, and on
  a hit rewinds `cmdline` to its own command token so the statement
  re-executes after IRETURN and RESUMES the remaining wait (the
  static `interrupted` flag, MM_Misc.c:660-686).
- Targets resolve through `GetIntAddress` (MM_Misc.c:10250): SUB
  name, else label, else line number.
- ON KEY has two forms (Commands.c:8255): the generic form fires
  when the console buffer is non-empty and the key STAYS in the
  buffer for INKEY$ inside the handler; the specific form
  (`ON KEY ascii%, handler`) fires only on that key, which is
  CONSUMED — it never enters the buffer (PicoMite.c:932-935).

The semantics worth copying exactly, in one line each: statement
granularity; one dispatch per boundary; no nesting; priority = scan
order; missed ticks dropped; error state saved, cleared and restored
around the handler; pin edges are level-compares, not latches.

## Why the same shape fits Fuzix (and signals do not)

The Unix-native shape — SIGALRM for ticks, raw tty + polling or
SIGIO for keys — was considered first and rejected on three grounds:

1. The kernel is non-preemptive and delivers signals at syscall
   boundaries.  A compiled BASIC compute loop makes no syscalls, so
   a busy FOR loop would never see SIGALRM.  The polled model has no
   such hole: the poll travels with the statements.
2. Granularity: `alarm()` counts seconds and `_pause()` deciseconds;
   SETTICK needs milliseconds (kernel tick is 5ms, TICKSPERSEC 200,
   config.h:264).
3. Atomicity: a signal handler lands mid-statement.  The runtime's
   string scratch stack, pixel queue and glyph queue are not
   reentrant, and MMBasic guarantees whole statements.  A statement
   boundary is exactly where everything is quiescent.

And the project rule settles it anyway: MMBasic is the proven
implementation of this facility AS a poll.  The port is the same
algorithm with the flags fed from Fuzix sources instead of ISRs.

## Constraints, in priority order

1. **Observable behaviour identical to MMBasic** for the supported
   subset — the triage rule: a silent divergence outranks a gap.
   The two honest divergences are named below (INPUT, console poll
   decimation), not slipped in.
2. **Zero cost for programs using no interrupt feature.**  Emission
   is gated in the translator exactly as the ON ERROR arithmetic
   guards are (that gating is board-measured at +11.9%); the eclipse
   and dhrystone numbers must not move at all.
3. **No kernel changes in phase 1** — and none are needed: every
   phase-1 source is already readable from userland.
4. **The gates run the same code** (fallback clock, stub pins);
   nothing is claimed until the board confirms it side by side with
   a real PicoMite.

## The framework

Three layers, smallest first.  Nothing changes in bcrun: the poll is
emitted into the generated C, so bytecode and native-backend
functions get it identically.

### Translator (mmb2c.py)

- Scan pass sets `uses_interrupts` when it sees SETTICK, ON KEY,
  SETPIN with an INT mode, or IRETURN — a sibling of `uses_onerror`
  (mmb2c.py:593).
- `statement()` already appends a per-statement epilogue for ON
  ERROR (mmb2c.py:2658), with the block-opener/closer placement
  subtleties solved.  Interrupt-using programs get one more line in
  the same place, AFTER the error guard — the interpreter's own
  order (statement, error bookkeeping, then check_interrupt,
  MMBasic.c:1852-1879):

      if (__mm_int_armed) mm_int_poll();

- New statements to translate: SETTICK in all forms (period+target
  [+id], PAUSE, RESUME, 0 = off); ON KEY both forms and their off
  forms; SETPIN pin, INTH|INTL|INTB, target.  IRETURN compiles to
  `mm_int_ret(); return;`.
- Targets must be literal SUB names taking no parameters.
  `GetIntAddress` also accepts labels and line numbers, but compiled
  code cannot jump into the middle of a function from a poll site;
  those are refused at translation with a clear message, the ON
  ERROR RESTART precedent (mmb2c.py:3894).  Handler SUBs are
  otherwise ordinary generated functions and may also be called
  normally.

### Runtime (new mmb_int.h, plus small hooks in mmb_runtime.c)

File-scope statics in a header, the mmb_gpio.h pattern: cc1 emits
nothing for a static nothing names, so only a program that says
SETTICK carries the machinery.

State — per process, which is a sentence MMBasic could never write:
every BASIC program owns its own interrupt table, and two programs
can each run their own SETTICKs:

    mm_ticks[4]   { active, period_us, due (64-bit us), fn }
    mm_pins[N]    { pin, edge, last, fn }
    mm_key_any_fn;  mm_key_sel, mm_key_sel_fn
    __mm_int_armed (count), __mm_in_int (flag)

`mm_int_poll()` returns immediately when `__mm_in_int` (MMBasic's
InterruptReturn gate, MM_Misc.c:10242 — no nesting; a handler's own
statements still carry polls, and the gate makes them no-ops), then
scans in checkdetailinterrupts' order restricted to the subset:
specific key, any key, pins, ticks.  At most one dispatch per call.

**The clock.**  `mm_us64_fast()`: TIMERAWH/TIMERAWL (RP2350 TIMER0
+0x24/+0x28; TIMERAWL is already read directly at
mmb_runtime.c:3849) with the high/low/high consistency loop — three
loads, no syscall, no wrap problem for any period MMBasic allows.
Gated by `mm_have_fastclk()` (mmb_runtime.c:3873, PICOIOC_BOARD);
host and gates fall back to `mm_us_now()`, so the same code runs
everywhere — the pixel-queue precedent exactly.  PICOIOC_ADVAL
selectors -9/-10 remain as the syscall fallback if ever needed.

**Ticks.**  Fire when now reaches `due`; then `due += period` until
`due > now` — MMBasic's catch-up loop (MM_Misc.c:10176), missed
periods dropped, not queued.  PAUSE stores `remaining = due - now`;
RESUME sets `due = now + remaining` — the frozen-count semantics of
`TickActive`.  Ids stay 1-4.

**Keys.**  The any-key form is "console input queue non-empty":
`ioctl(0, TIOCINQ)` — already implemented in the core tty
(Kernel/tty.c:313), zero kernel work.  The key stays queued for
INKEY$ in the handler, which is MMBasic's rule.  A syscall per
statement would be ruinous in a tight loop (the trap floor is
~600ns, PLAN-pixel-batch), so the console check is DECIMATED by the
fast clock: at most once per MM_INT_CON_US, default 5000us — the
kernel tick.  Worst-case added latency 5ms, invisible against human
typing; this is divergence #1, named.  The specific-key form must
pull bytes through the escape decoder: TIOCINQ first, then the
mm_inkey dance (mmb_runtime.c:2809 — per-call termios flip, so
INPUT and canonical reads are untouched) to decode one key; a match
fires and is consumed (never re-queued — PicoMite.c:932), a
non-match goes onto a small decoded-code FIFO that INKEY$ drains
first, in front of mm_kq.

**Pins.**  SETPIN's INT modes extend mmb_gpio.h's mode table
(MMG_PIN_INTH/INTL/INTB): claim once through MM_GPIO_CLAIM — so the
pin resets when the program exits or dies, nothing given back by
hand — then `pc3_pin_in`, and `last` seeded from the pin as
External.c:2050 does.  The poll is one register load per armed pin
and MMBasic's exact comparisons: `v > last` INTH, `v < last` INTL,
`v != last` INTB, `last` always updated.  Statement-rate polling
misses pulses shorter than a statement — MMBasic's identical,
documented limitation, replicated rather than "fixed".  The DS3231
alarm line on GP32 is active-low, so an RTC alarm is nothing more
than `SETPIN 32, INTL, AlarmSub`.

**Dispatch.**  `mm_int_fire(fn)` replicates GotAnInterrupt and
cmd_ireturn minus the trampoline, which a compiler does not need —
the handler is a function, so call/return replace the synthetic
IRETURN gosub (MM_Misc.c:10205), and C locals replace the
g_LocalIndex bookkeeping:

    save + clear the __mm_e error slot and the ON ERROR skip
      counter        (Saveerrno/SaveErrorMessage/SaveOptionErrorSkip,
                      MM_Misc.c:10190-10198)
    __mm_in_int = 1
    fn()             IRETURN inside is mm_int_ret();return;
                     END SUB falls off the end; both land here
    restore error state; __mm_in_int = 0

IRETURN with `__mm_in_int == 0` is the runtime error "Not in
interrupt" (MM_Misc.c:1848).

**Long statements.**  `mm_pause` (mmb_runtime.c:2128) on the FCC
path chunks its sleep to min(remaining, next tick due, one
decisecond) and polls between chunks and inside the sub-decisecond
spin.  Because the handler is a direct call, the pause simply
CONTINUES afterwards — the observable behaviour MMBasic buys with
its rewind-and-reexecute trick (MM_Misc.c:660-686), without the
trick.  INPUT blocks in the kernel with no polls; phase 1 documents
that interrupts stall until the line is entered — divergence #2,
named.  The fix, if a program ever needs it, is a VMIN=0 line-input
loop in one function.

### Kernel

Phase 1: **nothing**.  TIOCINQ exists, the microsecond counter is a
register read, pins are claim-and-read through pinlock and
pc3io.h, and the capability gate is the existing PICOIOC_BOARD.

Phase 2 candidates, each a read of state the kernel already keeps:
PLAY-done via SNDIOC_PCMOWNER (pico_ioctl.h — poll the owner
returning to 0), serial COM rx-level via TIOCINQ on /dev/ttyN, and
nothing at all for the RTC because the pin path covers it.

## Source map

| MMBasic source            | Here                                | When    |
|---------------------------|-------------------------------------|---------|
| SETTICK x4                | us deadlines off the fast clock     | phase 1 |
| ON KEY (any key)          | TIOCINQ on fd 0, decimated          | phase 1 |
| ON KEY key%               | decode-and-match, key consumed      | phase 1 |
| SETPIN INTH/INTL/INTB     | claim once + register poll          | phase 1 |
| COM rx level              | TIOCINQ on the port                 | phase 2 |
| WAV/PLAY done             | SNDIOC_PCMOWNER poll                | phase 2 |
| PS2 / keypad / IR / nunchuk | no such input path on the PC3 —  |         |
|                           | USB keys arrive through the console; ON KEY covers them | — |
| PID, PIO, DMA, ADC, I2C slave, GUI touch, collisions, web | the features they notify do not exist in this environment | — |

## Performance, and the native-code cliff

- No interrupt feature used: zero emitted bytes, zero cycles.  This
  is constraint 2 and the checks-gating precedent carries it.
- Armed: one global load + branch inline per statement.  Inside
  `mm_int_poll`: ticks are one clock read against a cached
  earliest-due, pins one load each, console at most one syscall per
  5ms.  Sub-microsecond without the syscall.
- The real risk is CODE GROWTH, not cycles: the native backend drops
  a function to bytecode past its size limit — 2.7x, board-only,
  invisible on the host.  One extra call site per statement will
  push some functions over.  Mitigations in order: emission is
  per-program gated; the poll is the minimal if-flag-call shape; and
  if a real program still falls off the cliff, restrict emission to
  loop back-edges and time-consuming statement classes — MMBasic's
  own effective granularity is the 1ms tick anyway.  Decide from
  measurements, not taste: fresh boot, interleaved A/B, screen
  output (the benchmark method).

## Verification

- Host gates: the same code with the fallback clock; tick-count
  tests with generous tolerances; IRETURN, nesting-refusal and
  error-state tests are clock-free and exact.  qemutests.sh gates
  the native backend with polls emitted — that is the rule for
  anything touching emitted code shape.
- Board, side by side with the real PicoMite (the authority): one
  minute of SETTICK cadence; a handler LONGER than its period
  (catch-up must drop, counts must match); PAUSE 5000 under a 100ms
  tick (handler cadence during the pause, pause still ends on
  time); ON KEY latency and buffer semantics, both forms; INTH/
  INTL/INTB against a driven pin including the too-short-pulse miss;
  MMerrno cleared inside and restored after a handler; IRETURN
  outside an interrupt errors; two sources pending at once dispatch
  one per boundary in priority order.
- One change at a time, and confirm the board runs the build you
  think it does, before any number is believed.

## Open questions

1. Keep NBRSETTICKS at 4 (replicate), though nothing here makes
   raising it hard.
2. ON KEY when stdin is a file or pipe (RUN from fm, the demo
   driver): isatty(0) is false and TIOCINQ is a tty ioctl.
   Proposal: armed but never fires — the same quiet answer INKEY$
   gives there today (mmb_runtime.c:2833).
3. MM_INT_CON_US = 5000: matches the kernel tick and human input; a
   #define, not an OPTION, until a program proves it matters.
4. Docs when it ships: PC3-C-MANUAL section, coverage.py rows, and
   the book's interrupts chapter can then teach the real thing.

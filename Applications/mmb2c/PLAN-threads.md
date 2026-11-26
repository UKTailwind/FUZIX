# Threads for translated MMBasic: an evaluation

26 November 2026. Whether MMBasic V8's threads (`THREAD START` and the rest, new
on the Colour Maximite 2 and the PicoMite this week) can be had by a translated
program on a PC3 running FUZIX, and through pc3host on Linux and Windows.

Read for it: the Threads User Manual (CMM2V1.5 `docs/threads/text`, chapters 1
to 5, 8 and Appendix A), the CMM2's design and progress notes
(`docs/Threads_Plan.md`), the PicoMite's review
(`docs/Threads_PicoMite_Review.md`), and our own bcrun and runtime. Nothing
here has been built or run.

## The answer

Yes, on all three, and our design fits threads better than the firmware's did
in the places that cost the firmware most. The way to do it is the firmware's:
one BASIC thread runs at a time, switched only between statements, each thread
with a C stack of its own. Inside bcrun they are green threads: one process on
the board, one OS thread on a PC. The manual's promises then hold unchanged: a
statement is never split, a wait lets the other threads run, an interrupt
belongs to the thread that set it up.

The PC builds come first and are straightforward. The board is possible too,
with two costs that need measuring there: memory (about 12 to 20 KB a thread
from the process pool every program shares) and the clock (a system call, too
dear to read at every statement to end a slice).

## Why it fits

| The firmware had to | Here |
|---|---|
| Swap the interpreter's tables for locals, loops and calls at every switch: `gosubstack`, `forstack`, `dostack`, `hashlist`, `LocalIndex`, slot ranges, `SymL` (its biggest design area, and the reason for `LOCALS` and `NEST`) | A translated program's locals, parameters, FOR, DO and WHILE loops and SUB and FUNCTION calls are C locals and C control flow on the VM stack. A thread with a VM stack of its own has them all for nothing |
| Make `longjmp(mark)` safe from any thread's stack | Nothing to unwind: `mm_error` records the error and returns, and the generated code skips the rest of the statement (`mmb_runtime.c`, "ON ERROR SKIP / IGNORE") |
| Make OPTION COMPILE's bind caches stale at every switch (`BindsStale`) | No counterpart |
| Add the switch to the statement tail | Every statement already ends in `if (__mm_int_armed) mm_int_poll();`. The switch goes in the poll. A program that starts no thread pays what it pays now: one load and a branch not taken |
| Keep interrupt routines from nesting, with one saved error state | Already so: `__mm_in_int` and `mm_int_err_push` |
| Measure PAUSE against the microsecond clock, not a shared counter | Already so (`mm_pause_raw`) |

Today `THREAD` is an unknown command to both translators, so a program that
uses threads is refused when it is translated. Nothing breaks meanwhile.

## Why a C stack each, even on a PC

On a PC bcrun runs no native code (the Thumb spans are `__arm__` only) and
`BC_CALL` is iterative, so while a program runs the C stack holds one `bc_exec`
and, during a library call, the runtime function. Swapping bcrun's registers
(`A`, `pc`, `sp`) would be enough to switch at a poll site.

It is not enough for the waits. PAUSE, INPUT and LINE INPUT (console and serial
port), the frame waits, THREAD LOCK and WAIT and a WEB request are C loops in
the runtime, and the manual promises that the other threads run while one of
them waits. With a C stack each, such a loop calls the scheduler from inside
itself and carries on when its thread's turn comes back. This is the CMM2
plan's own reasoning ("Why each thread needs its own C stack"). On the board
it is needed whatever the waits do: native spans put real ARM frames between
`bc_exec` and the runtime (`native_enter`, and `helper_call` back into
`bc_exec`).

| | The switch | Notes |
|---|---|---|
| Linux | `swapcontext` (glibc), or about 15 lines of assembler each for x86-64 and aarch64 | `swapcontext` also makes a `sigprocmask` call, well under a microsecond: nothing at 1 ms slices |
| Windows | Fibers: `ConvertThreadToFiber`, `CreateFiber`, `SwitchToFiber` | The supported way. It keeps the stack limits that the compiler's stack probes and SEH rely on, which a hand-made switch would not |
| Board | About 20 lines of Thumb-2: push r4 to r11 and lr (and d8 to d15 if the build uses the FPU's registers), store sp, load the other, pop | On `cpu-armm0` the brk limit is the process top (`brk_limit()` is `p_top`), not the stack pointer, so a stack malloc'd in the process does not stop `sbrk`; the kernel sets no `PSPLIM` |

## What each thread keeps

**In bcrun**: the VM registers `A`, `pc`, `sp` and `stack_floor`, a VM stack,
and its C stack. A program address is a machine address now, so a thread's VM
stack can be allocated anywhere, outside `mem[]`. A new thread's C stack starts
in a small entry function that runs the translated thread's SUB under a
`bc_exec` of its own; when that returns, the thread is finished and switches
away for good, and the next thread to run frees its stacks.

**In the runtime**, a first pass (a complete inventory of every static in
`mmb_runtime.c` and the `mmb_*.h` headers is Stage 0's work, as it was the
CMM2's):

| State | Where | Why per thread |
|---|---|---|
| String temporaries and by-reference temporaries | `mm_pool`, `mm_pool_top`, `mm_byref_pool`, `mm_byref_top` | They are stacks, wound back by `mm_mark` and `mm_release`. Shared, a FUNCTION returning in one thread would release another thread's live temporaries. Each thread needs its own pool |
| Error state | `mm_errno_v`, `mm_errmsg_v`, and the program's `__mm_e` through `mm_est` (the ON ERROR SKIP count and the poison flag) | ON ERROR, MM.ERRNO and MM.ERRMSG$ are per thread (manual, chapter 4) |
| The DATA pointer | `mm_dn`, `mm_dptr` (`mmb_data.h`) | Each thread reads from the start, or from its own RESTORE |
| GOSUB's returns | `mm_gstack`, `mm_gsp` (`mmb_misc.h`) | A return belongs to its thread |
| Print position, colours, font, PRINT @ mode, FRAMEBUFFER WRITE target | `mm_charpos`, `mm_gfx_fg`, `mm_gfx_bg` and others; partly held in pc3host's server or the kernel | Per thread by the manual. At a switch the runtime flushes what it has queued (`mm_gflush`) and sends again whatever differs from what the server holds; it already keeps the last colour sent (`mm_gfx_col`) |
| INPUT's line | `mm_inbuf` | Two threads in INPUT at once would share it: the console gets one owner at a time, the second INPUT waiting for the first |
| Results in static buffers | `mm_fnbuf`, `mm_devbuf`, `mm_pathbuf`, `mm_cmdbuf`, `mm_sink_s` | Each to be checked. A switch never comes inside a statement except through one of the program's own FUNCTIONs, which could already overwrite them today |

Shared by design, as in the firmware: global variables and STATICs, files and
channels, DIR$'s search, the keyboard, sound, sprites, BLIT buffers, the
serial ports, FLAG(), TIMER and RANDOMIZE's sequence.

## The scheduler

As the manual describes it: turns in number order with the main program as
thread 0; a turn of as many slices as the thread's priority; a thread whose
PAUSE has just ended goes first; a lock lends its waiter's priority to its
holder; with nothing ready the process sleeps until the earliest wake, so a
program of waiting threads takes no processor time.

**Ending a slice.** On a PC the poll reads the clock every few statements
(`clock_gettime` or `QueryPerformanceCounter`, some tens of nanoseconds). On
the board the clock is an `ioctl` (`lib_us64`, `PICOIOC_ADVAL` -10), dearer
than most statements, so the poll counts statements and reads the clock every
N of them, N chosen so that N statements take about a slice. The board's
slices are then approximate: a run of slow statements makes one longer.

**Waits that let the others run**: PAUSE; INPUT and LINE INPUT at the console
and on a serial port; THREAD LOCK, THREAD WAIT and THREAD WAIT FLAG; the frame
waits (`mmb_wait.h`); the WEB family's waits. Each of these loops calls the
scheduler. Everything else holds every thread until it ends, as in the
firmware: a file copy, LOAD PNG, SORT, a long MATH.

## Interrupts

Each interrupt the program arms (`mmi_*` in `mmb_int.h`) records the thread
that armed it. The poll in a thread runs only that thread's routines; one that
is due for another thread makes that thread the next to run. After a thread
ends, its routines run in the main program. There is no switch while a routine
runs, and a wait inside one is an error, all as the manual says.

## The translators

Both, byte for byte (cgate): `THREAD START n [SIZE kb] [LOCALS m] [NEST d]
[PRIORITY p], sub [, argument ...]`, `EXIT`, `STOP`, `YIELD`, `PAUSE`,
`RESUME`, `SLICE`, `PRIORITY`, `SIZE`, `LOCK`, `UNLOCK`, `HOLD`, `RELEASE`,
`WAIT n`, `WAIT FLAG f`, `LIST`, and `MM.INFO(THREAD)`, `MM.INFO(THREAD n)`,
`MM.INFO(THREAD LOCK k)`.

- The SUB and its arguments are known when the program is translated, so "x is
  not a SUB", "Too many arguments for a thread" and "Only a global array can be
  passed to a thread" are found then, not when the program runs. For each SUB
  that is started as a thread the translator writes a small entry function that
  takes the arguments from a block the runtime fills at THREAD START: values
  copied, strings copied, a global array by its address.
- `LOCALS` and `NEST` are accepted and change nothing (there is nothing of
  that kind to size). `SIZE` sizes the thread's stacks.
- ERASE, REDIM and CLEAR while a thread runs: "Not allowed while a thread is
  running". RUN and CHAIN in a thread: "Not allowed in a thread".

**Errors.** An error that is not caught ends the program in any thread, as now.
A translated program does not know its line numbers as it runs, so the message
names the thread instead: `Error in thread 3: Index out of bounds`. THREAD
LIST and Ctrl-C's listing show each thread's SUB, state and what it waits for,
without a line.

**THREAD STOP** of a thread that is inside a wait takes effect when that
thread next reaches a poll or comes back from its wait, so a runtime loop is
never left half way with a file or a buffer in hand. This is the CMM2's own
suggested mend for the same problem in its WEB commands.

## Memory

| | A PC | The board |
|---|---|---|
| C stack | 256 KB to 1 MB (pages taken only as they are touched) | 4 to 8 KB, to be measured: LOAD PNG and JPG, JSON$, the float formatter go deepest |
| VM stack | 64 KB | 4 KB (the main program's is 8 KB, `STACKROOM`) |
| String temporaries | 64 KB (256 of them, `MM_TMPN`) | 4 to 8 KB (16 to 32) |
| Threads | 16 (D2) | 16 (D2); memory is taken only for a thread started |

On the board, 12 to 20 KB a thread comes from the process memory every program
shares (`TOTALMEM` 340 KB). The stacks belong in SRAM: in PSRAM, where the heap
is, every local would be about 3.7 times slower to reach.

## The size of the work, in stages

| Stage | What | Where |
|---|---|---|
| 0 | The inventory of the runtime's statics; bcrun's switch on Linux; per-thread VM and C stacks | bcrun, about 250 lines with the three switches |
| 1 | THREAD START, EXIT, STOP, YIELD, PAUSE, RESUME, SLICE, PRIORITY, LIST, the MM.INFO forms; the per-thread runtime state | runtime about 600 lines; each translator about 300 |
| 2 | LOCK, UNLOCK, HOLD, RELEASE, WAIT, WAIT FLAG; PAUSE, INPUT, LINE INPUT, the frame waits and the WEB waits letting the others run | runtime about 400 lines |
| 3 | Interrupts owned by their thread | `mmb_int.h` |
| 4 | Windows (Fibers); the board (Thumb switch, statement-counted slices, sizes measured there) | |
| 5 | The manual's chapter; the release | |

The firmware's own `Thread.c` is 1,439 lines on the CMM2 and 1,647 on the
PicoMite, with changes elsewhere. Ours should be smaller, having no locals or
bindings to manage, and the estimates above are guesses at that scale.

**Tests.** The PicoMite's `Bas/threads` has 63 programs, 60 of them with
expected output. Those for things a PC3 has (not SQL, flash, pins, Bluetooth or
TEMPR) become a corpus run on Linux and Windows, and then on the board, with
the firmware's outputs as the oracle.

## Risks

1. **State missed from the inventory.** A static that one statement sets and a
   later statement of the same thread relies on would be trampled by another
   thread. The firmware's tests run every control structure in two threads with
   a yield after every statement; ours should too.
2. **pc3host's drawing state.** Colours, font and the write target live partly
   in the server. Sending them again at a switch only when they differ keeps it
   cheap, but every place that sets one must go through the runtime's record.
3. **The board's memory and clock**, above. Both are measured in Stage 4,
   before the board's limits are settled.
4. **Programs.** Shared loop variables and two-statement updates without a lock
   are the usual mistakes, as on the firmware. The manual's chapter on sharing
   applies unchanged.

## Decisions

Peter, 26 November 2026.

| | Question | Decided |
|---|---|---|
| D1 | Include threads? | Yes: Linux first, then Windows, then the board once it has been measured |
| D2 | How many threads? | 16, on a PC and the board alike (the CMM2's limit: 1 to 16). First said 8, then 16 |
| D3 | `LOCALS` and `NEST`? | Accepted and ignored, so that firmware programs translate unchanged |
| D4 | The board's slices? | Counted in statements with the clock read every N, for a first build |
| D5 | The error message? | `Error in thread n: message`, as we have no line numbers |

## Progress

### 26 November: Stages 0 to 3 on Linux and Windows

Built as designed above, with these particulars.

- **The runtime**: `mmb_thread.c`, included at the end of `mmb_runtime.c`.
  The switch is `swapcontext` on Linux and Fibers on Windows; anywhere
  else (the board, fcc's own compile of the runtime) the THREAD calls are
  stubs and `THREAD START` says "Threads are not available on this
  machine yet".  bcrun swaps its VM registers (`A`, `pc`, `sp`,
  `stack_floor`) and gives each thread a VM stack from the VM heap
  (`bcrun_mm.c`, `mmx_vm_*`).
- **The program's half**: `mmb_thread.h` (the DATA pointer, GOSUB's
  returns and FLAG()'s word bound per thread; the waits, sliced as PAUSE
  is), and `mmb_int.h` under `MM_THREADS` (the turn asked for every 16
  statements; an interrupt owned by the thread that set it up, queued for
  it and run there).
- **Per thread**, besides the plan's list: the LOCAL frames (the native
  build's arena is the main program's alone; a thread's frames are listed
  with it, so STOP and EXIT give them back), and the screen: colours,
  font, the graphics cursor, PRINT @'s mode and FRAMEBUFFER WRITE, which
  a new thread takes from its starter and MODE gives to all.
- **The clock**: the scheduler has its own monotonic one.  The runtime's
  is the PC3's, a round trip to the device server under bcrun on a PC.
- **The rotation** keeps its own place: a thread whose PAUSE has ended
  goes first without moving it on, or a program that PAUSEs in a loop
  beside a busy thread hands every turn to that thread (found by the
  firmware's tprio).
- **Waits that let the others run**: PAUSE, the THREAD waits, INPUT and
  LINE INPUT at the console (poll on the descriptor, glibc's and msvcrt's
  stdio buffers looked at first) and on a serial port.  Not yet: the
  frame waits, PLAY's, the network's.
- **A wait with a SETTICK armed sleeps until the tick is due**, not for
  its period (`mm_int_slice_us`): slices cut from where the wait began
  drifted against the deadlines, by half a millisecond a sleep on Windows.
  This applies to every program, threads or not.
- **Windows** sleeps on a high-resolution waitable timer: Sleep's 15.6 ms
  tick made a 2 ms PAUSE take 15.6.
- **MM.INFO(FCOLOUR) and (BCOLOUR)**, which the firmware's tscreen reads,
  were not translated before.

Tests: the firmware's own `Bas/threads`, through mmb2c.py, fcc and
pc3host's bcrun: every one whose subject a PC has passes, apart from
EXECUTE, EVAL and MM.INFO(VARCNT) (not translated) and THREAD LIST's line
numbers.  Fifteen of them are `tests/thr*.bas`, with `thread8.bas`,
`threadin.bas` and `threrr8.bas` of ours; pc3host's e2e runs `threads.bas`
(INPUT with the line given 0.3 s late) on Linux and Windows.

To do: the board (a Thumb switch, the statement-counted slice, the sizes
measured); the frame, PLAY and network waits; the poll's cost with a
thread running (an empty loop runs at a quarter of its speed under
bcrun: the call to `mm_int_poll` every statement).

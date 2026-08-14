# The picofrog forward-move crash

Status: **open**, parked 2026-08-14.  Two real bugs were found and fixed
on the way (below); the crash itself is not one of them.

## The symptom

picofrog (the no-sound build, `pfns.bas`) starts, draws its panel, runs
its lanes and its timer, and plays.  Moving the frog **left or right is
fine**.  On a **forward** move the machine dies:

* the serial console goes completely dead - not one byte, no echo
* the display keeps running, showing the last frame
* a loud noise comes out of the speakers
* a warm reset throws an exception; only a power cycle recovers it

The noise and the live picture are both what a machine that has stopped
servicing anything looks like: the I2S and scanout DMA keep running on
whatever they were last given.  The very first crash of the day made the
same noise before a single note had been asked for, which is what rules
sound out as a cause.

Ctrl-C does not help, which rules out a plain userland loop: the kernel
would still be scheduling and the console would still answer.

## Why forward, and not left or right

The move is two halves, three frames apart:

    FJump          SetTick 20,jump,1 / FRAMEBUFFER WRITE L / Box /
                   Sprite write FPOSP%+o%
    Move_Player    when FMOV% counts down to 1: Hide_Frog / the
                   direction branch / GoTo fmout / Sprite write
                   FPOSP%+1

Both halves run for every direction.  The direction branch is the only
difference, and only the forward one scores:

    If FPDIR%=1 Then
      Inc FPOSY%,-8
      If FPOSY%< FMAX% Then Inc Score%,10:FMAX%=FPOSY%:write_score

So `write_score` - and through it `write_High` - runs on forward moves
and on no others.  That is the whole of the left/right asymmetry, and it
is why the first suspicion fell on TEXT.

## What the traces say

Diagnostics have to go to `/dev/tty`, and every line needs a `Pause`
after it (see "Two traps" below).  With `pfauto.bas` - picofrog with the
keyboard replaced by a counter that presses up by itself, and a trace at
every statement of the move - three runs said three different things:

1. died at `Text 240,88, S$` (last line `WS 6 pre-text`)
2. with the same TEXT done first from a **literal** and then from the
   local: died at the **literal** (`WS 6a literal`).  So the string's
   memory is not it - a literal lives in the program's data segment and
   a LOCAL comes from the VM heap, and they fail the same way
3. with two more TEXT calls added around `FJump`'s sprite write: the
   first `write_score` **completed** - both TEXT calls, through to
   `WS 10 leaving` - and the machine died on the **second** move, at the
   first of the added calls

Run 3 is the important one.  **The death point moves when the timing
moves.**  It is not a fixed bad pointer in a particular statement; the
statements that die are ordinary and have already run successfully.

## Ruled out

Each of these was tested, not argued:

* **Sound.** No PLAY statement survives in this build.  The remaining
  SETTICK handlers only count.
* **Pins.** Nothing in the program touches GP10/GP11/GP22 - every NES
  and Wii line is commented out as `PORT:`.  Those pins are the I2S DAC
  and only the kernel drives them.
* **Native code.** `BCRUN_BYTECODE=1` crashes identically, so the Thumb
  backend is not involved.
* **Where the string lives.** See run 2 above.
* **Sprite numbers.** Every sprite written (18-25, 30, 63) is created by
  a `Sprite read`; MMS_MAX is 64.
* **Glyph indexing in the kernel.** `display_gfx_text` bounds the
  character range and fills the cell with paper for anything outside it.
* **The string pointer crossing.** `GFXIOC_TEXT` blesses it with
  `valaddr_r` and returns EFAULT - a bad address there draws nothing, it
  does not fault.
* **A leaked LOCAL block on EXIT SUB.**  Scanned the generated C
  mechanically (`lheapscan.py`): every `return` inside a routine that
  owns an `mm_lheap` block frees it first.
* **The tick catch-up loop.**  `do { due += period } while (due <= now)`
  would spin forever at period 0, but `SETTICK 0` clears `active` and
  the scan skips it.
* **A userland loop** (Ctrl-C would work) and **the display** (core1 is
  autonomous and has been proven repeatedly).

## Where the evidence points

Process size.  The failures are monotonic in it:

| build | loader allocations | outcome |
|---|---|---|
| picofrog with sound | ~191K | `panic: swapin: no memory` at load |
| no-sound | ~145K | loads and runs; dies on the first forward move |
| scoreprobe (the same write_score code, alone) | small | 10/10 fine |
| every other sample | small | fine |

The first failure of the day was the swapper itself, saying so.
`PROGSIZE` is 346,624 against a 340K pool, so a big program leaves less
than one 4K block for anyone else, and picofrog is the first program
this port has run that comes near it.  Swaps happen when another process
needs to run - which console I/O provokes - and that is exactly the kind
of timing dependence run 3 showed.

`pagemap_realloc` does have a ceiling that reserves room for
`largest_neighbour()`, and the load-time panic proves a process can
still reach a state where `swapin` cannot place it.  Note also that
`swapin` **panics** where it cannot fail gracefully: a userland program
that asks for too much should be refused, not take the machine down.

That is the thread to pull next.  It has not been proven, and the honest
statement is that the mechanism is not yet known.

## Suggested next steps

* Reduce the process and see whether the crash moves or goes: building
  the same program with the native backend off shrinks the code buffer
  by roughly half, changing nothing else.
* Instrument the swapper (its DEBUG kprintfs) on a kernel build and
  watch whether picofrog is being swapped at the moment it dies.
* Make `swapin`'s failure survivable, or make the ceiling that is meant
  to prevent it actually sufficient.
* A different sprite-using BASIC program, as a second data point from a
  different angle.

## Two traps that cost hours

* **After `MODE 2`, `PRINT` draws on the display and never reaches
  stdout.**  Not the console, not a redirect, not a file - the graphics
  screen.  Every diagnostic in a graphics program has to be opened
  explicitly: `Open "/dev/tty" For Output As #2`.  This is why the
  machine appeared to die silently every time.
* **The console transmits in the background.**  Without a `Pause` after
  each trace line the machine dies with the line half sent - the first
  run ended at `P2 ws i` - and the last thing printed is not where it
  got to.

## Fixed on the way (both committed, both board-verified)

* **`TEXT` with no font used font 1 instead of the current one.**
  `cmd_text` (Draw.c:2133) takes font, scale and both colours from the
  current state; the translators emitted a literal 1.  A program that
  says `FONT 10` once and then draws with the plain four-argument TEXT -
  which is how MMBasic programs are written - got font 1 every time.
  This hid DefineFont completely: user fonts worked when named on the
  TEXT itself, and no ordinary program could show it.  `fontuse.bas` is
  the test, counting ink with PIXEL(): 25/64 before, 64/64 after.
* **picofrog took the VGA arm of the original's buffer model.**  With
  `lcd%=0` the frog is drawn into the LAYER and never merged, because on
  a PicoMite VGA the layer is composited at scanout.  We implement the
  merge model deliberately, so the frog was invisible.  `lcd%=1`, MODE 2
  on both arms, and the per-frame merge moved out of the music tick.

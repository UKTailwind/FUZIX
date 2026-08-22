# PLAN-pioout: WS2812 and BITSTREAM through PIO1

2026-08-22.  The user's proposal, adopted: PIO1 already runs the I2S
sound program on one state machine and a handful of instructions; load
fixed bitstream/WS2812 executor programs beside it at boot, on another
state machine, and let userland pre-calculate the timings into an
array and DMA it to the PIO.  This closes the two commands the
interrupts-off review (COVERAGE-STATUS NOTES[3]) relegated, with ZERO
interrupt impact and near-zero CPU: the machine keeps running while
the wire is driven, which the bit-bang route could never give.

Verdict on the approach: **sensible, and better than sensible — the
pre-calculated-array-plus-DMA shape is exactly how the hardware wants
to be used**, and it reuses everything the port already has: the
claim/death-sweep machinery (pinlock), the userland-drives-registers
philosophy (pc3io.h), and the counting inputs as the measurement
instrument for acceptance.

Reference lines cited below are `d:\Dropbox\PicoMite\PicoMite\misc\
External.c` (the current tree), read this session.


## 1. Is WS2812 "just a specific case of bitstream"?

Semantically yes — every WS2812 bit is two timed toggles — and
practically no, because of memory: as generic bitstream elements a
frame costs 2 words per bit = **64 bytes per RGB LED** (256-LED cap:
48K of array), while a dedicated PIO program consumes the packed
colour words directly at **4 bytes per LED** (256 LEDs: 1K).  The
executor also cannot reach WS2812's 0.05us granularity comfortably at
one word per edge.

So: **both**.  A generic timed-edge executor for `BITSTREAM`, and a
dedicated shifter program for `WS2812`, side by side in PIO1's shared
instruction memory.  They are 3 and 4 instructions each.


## 2. The hardware budget

**PIO block map** (the whole machine, so nobody collides later):

| block | owner |
|---|---|
| PIO0 | RESERVED for the user-PIO runtime (the separate-assembler plan) |
| PIO1 | kernel I2S (8 instrs, 1 SM, GP10/11/22) + **this plan** |
| PIO2 | CYW43 PIO-SPI (the radio's bus - never touch) |

**PIO1 instruction memory** (32 slots), all loaded ONCE at boot with
explicit origins so the offsets are compile-time constants in
pc3io.h:

| offset | program | instrs |
|---|---|---|
| 0-2   | bitstream, driven (`out pins`) | 3 |
| 3-5   | bitstream, open-collector (`out pindirs`) | 3 |
| 6-9   | WS2812 timing O (old '12) | 4 |
| 10-13 | WS2812 timing B ('12B) | 4 |
| 14-17 | WS2812 timing S/W (SK6812, W = 4 colours) | 4 |
| top   | I2S (origin -1, wherever the SDK put it - unchanged) | 8 |

26 of 32 used, 6 spare.  `sound.c` keeps origin -1; a new kernel
`pioout.c` loads the five fixed programs and asserts they landed at
their stated origins.

**State machines**: I2S holds its SM (claimed at sound_init).  ONE
further PIO1 SM is reserved at boot as *the output SM* — a single SM
runs ANY of the five programs (retarget = write a `jmp` to the
program's origin into SMx_INSTR at configure time), and one is enough
because these commands are one-at-a-time by nature (MMBasic's are
strictly serial too).  Two remain spare.

**DMA**: one channel, `dma_claim_unused_channel()` at boot beside the
sound pair, number recorded.  Completion is polled (channel BUSY then
SM TXSTALL in FDEBUG) — **no interrupts anywhere in this design**, and
the alarm pool is not touched.

**The pin window — a real constraint.** RP2350 PIOs see 32 of the 48
GPIOs through a per-block GPIOBASE.  I2S pins GP10/11/22 pin PIO1's
base at 0, so PIO1 reaches GP0–GP31: of the I/O header that is
**GP0–GP7 and GP26 only** — the same low-pin family as the counting
inputs, which keeps the story teachable ("the low header pins are the
timing pins").  GP34–46 cannot be a WS2812/BITSTREAM pin under this
plan; if that ever matters, PIO0 with base 16 is the escape hatch and
belongs to the PIO-runtime work, not this one.


## 3. The two programs

### 3.1 The bitstream executor (3 instructions)

One 32-bit word per edge: bit 0 = the level to drive (userland turns
MMBasic's toggle sequence into levels — the starting level is known,
so this is a trivial precompute), bits 31..1 = duration.

    .wrap_target
        out pins, 1        ; drive the new level ("the toggle")
        out x, 31          ; the duration, minus overhead
    delay:
        jmp x--, delay
    .wrap

Element time = 2 + (x+1) cycles, so userland stores
`x = cycles - 3`.  SM clock **20 MHz** (clkdiv 18.75 from 375 MHz —
representable exactly in 16.8 fixed point), giving a 50 ns grid, which
is finer than the reference's own SysTick grid.  Max duration
2^31 x 50 ns >> the 67,108 us element cap (4862+: `getint` range and
the "Number range" error replicate as-is).  Autopull on, threshold 32,
FIFO joined TX, DMA paced by the SM's TX DREQ.

The **open-collector variant** is the same program with
`out pindirs, 1`: dir=1 drives low, dir=0 releases to the pull-up —
MMBasic's mode 1 (4887-4897), including its "even number of
transitions" rule and the pull-up/pre-set-low pin dance, replicated in
userland before start.

### 3.2 The WS2812 shifter (4 instructions x 3 timing variants)

The classic side-set shifter, consuming packed colour words MSB-first
(autopull threshold 24, or 32 for the W type's RGBW):

    .side_set 1
    bitloop:
        out x, 1        side 0 [T3-1]
        jmp !x, do0     side 1 [T1-1]
        jmp bitloop     side 1 [T2-1]
    do0:
        nop             side 0 [T2-1]   ; wraps to bitloop

High time for a 0-bit is T1, for a 1-bit T1+T2; the T values are
instruction-encoded, which is why the three MMBasic timing sets
(External.c:4448-4473: O, B, S — and W = S with four colours) are
three resident program copies rather than one patched at run time —
nothing ever writes PIO1's instruction memory after boot.

**Exactness, stated honestly:** the reference expresses its half-bit
times in 0.05 us units (O: 0.35/0.70/0.65/0.475; B:
0.40/0.80/0.70/0.325; S: 0.30/0.60/0.75/0.45).  At a 20 MHz SM clock
the grid is that same 0.05 us, but the fixed-period structure of the
shifter cannot reproduce MMBasic's *different* bit periods for 0 and 1
exactly; one or two half-times land one cycle (50 ns) off.  The chips
tolerate +/-150 ns, so nothing observable changes on the wire's
receiving end — recorded here because "replicate exactly" is the house
rule and this is a knowing, bounded, physically invisible deviation.
(The alternative — WS2812 through the exact-grid bitstream executor —
is available to a user who cares, at 64 bytes/LED.)

**The reset latch**: the reference enforces TRST (50/280/80 us by
type) *before the next frame*, not after the current one
(4528: the `endreset` spin precedes the send).  Same here: a static
last-emit timestamp per pin, waited on at the head of the next call.


## 4. Ownership and lifecycle

Everything follows the counting-inputs precedent:

* **Boot** (`pioout.c`): load the five programs at fixed origins,
  claim the output SM and the DMA channel, record their numbers,
  assert PIO1 GPIOBASE == 0.
* **Claims per call** (existing `mm_gpio(MM_GPIO_CLAIM, ...)` — the
  PLKIOC path takes any class, so **no new bcrun libcall**):
  - `PLK_PIN` on the output pin (GP0-7/GP26; `claimable()` already
    allows exactly these low header pins),
  - `PLK_PIO`, idx = pio*4+sm for the reserved output SM —
    `claimable()` currently refuses PIO wholesale; it learns to allow
    exactly this one,
  - `PLK_DMA`, idx = the reserved channel — same.
  Two programs arbitrate through the claims; "already ours" is
  success, as ever.
* **Userland drives registers** (pc3io.h grows a PIO section: SM
  CLKDIV/EXECCTRL/SHIFTCTRL/PINCTRL, TXF, FDEBUG, and the program
  origins).  THE ONE HARD RULE: PIO1's CTRL register is shared with
  the I2S state machine, so it is touched **only through the atomic
  set/clear aliases** — a plain read-modify-write there can stop the
  audio.  The header provides only the atomic accessors, so the safe
  spelling is the only spelling.
* **Death sweep**: `reset_one(PLK_PIO)` disables and restarts the SM
  (atomic aliases); `reset_one(PLK_DMA)` aborts the channel.  A
  program killed mid-frame leaves a half-lit strip and a clean
  machine, which is the correct trade.
* **Blocking semantics**: the BASIC statement waits for completion
  (DMA not BUSY, then TXSTALL set), because MMBasic's does — but only
  the calling PROGRAM waits; the machine no longer does.  The wait
  polls with the same courtesy sleeps the WEB client waits use.

**DMA source addresses — CORRECTED BY THE BOARD, 2026-08-22.**  The
paragraph that stood here said BASIC arrays live in SRAM so the DMA
could read them in place.  WRONG, and the board said so on the first
run: **this kernel's swapper rearranges the process pool in 4K chunks
on EVERY context switch** (swapper.c's opening comment — it is how
every process runs at the same PROGLOAD), so a user array's physical
bytes move whenever its owner sleeps or is even preempted.  The DMA
then reads whichever process's chunks landed underneath — a garbage
duration word parks the SM in its delay loop for most of a minute —
and on switch-in the bytes shuffle back, so the array always LOOKS
intact afterwards.  Diagnosed with pioprobe2's cold matrix: busy-poll
streams ran at exactly full speed, sleeping streams froze at the
initial FIFO burst, and even "passing" busy-poll runs lost the 1-9
words that moved during timeslice preemptions.

So the DMA reads a **kernel-owned PSRAM buffer** (the arena heap never
moves): 10000 words — BITSTREAM's cap — reserved at pioout_init,
address answered by `GPIOC_PIOOUT_BUF` (0x053D, ioctlcheck 73).
Userland copies its words in (a store; no MMU) and points the DMA
there; the PLK_PIO claim arbitrates the buffer exactly as it does the
machine.  DMA-from-PSRAM through the QMI is now board-proven at these
rates (one word per >=1us element, 33k words/s for WS2812 — the
scanout shares the QMI but the FIFO absorbs and the numbers are
noise; a pathological all-1us BITSTREAM is ~4MB/s and is noted, not
feared).


## 5. The BASIC surface (MMBasic-exact)

    WS2812 type, pin, nbr, colours%()      ' type is O|B|S|W, unquoted
    WS2812 type, pin, 1, colour%           ' nbr=1 takes a scalar (4482-4486)
    BITSTREAM pin, n, array() [, mode]     ' n 1..10000; us values 0..67108
                                           ' mode 0 drive (default), 1 OC

Replicated to the letter: nbr 1..256 (4477); GRB(+W) byte order as the
reference packs it (4490-4520 — re-read at implementation, not from
memory); float OR integer arrays for BITSTREAM (parsenumberarray,
4898); "Array too small", "Number range", the OC even-count error;
BITSTREAM auto-configures an unconfigured pin as DOUT and refuses one
configured as anything else (4880-4883, mapped onto our claim model).
The **two-pin BITSTREAM form** (4934+) stays refused by name — it is a
synchronized dual-channel engine, rare, and a clean later addition as
a second SM if ever asked for.

Translator: two new statements in both translators (cgate 0 diff),
program-side `mmb_pioout.h` with the claims, the precompute, the SM
setup and the wait; host build models the transaction (arrays
validated, nothing driven) so the gates run.


## 6. Acceptance (COM14, GP2 -> GP4 — the counting inputs are the instrument)

The feature shipped last is the measurement rig for this one:

1. `BITSTREAM gp2, n, a()` with n known edges at 1 kHz-ish rates ->
   `SETPIN gp4, CIN, 3` counts exactly n.  Element-duration truth:
   a two-element 1 ms/1 ms square -> `SETPIN gp4, FIN` reads 500.
2. Long-element truth: 67 ms elements -> PER reads the period.
3. OC mode into the pull-up, DIN reads the released level.
4. WS2812: a frame of L LEDs = 24L data bits -> CIN on both edges
   counts 2x(bit count) edges within the strip's encoding (count
   depends on data — drive all-zeros and all-ones frames, whose edge
   counts are exact and different).  Rate ceiling note: WS2812 edges
   arrive at ~1.6 MHz worst case; if the priority-0 count IRQ measures
   short, count a SHORT frame — totals are what is being proven, not
   the ceiling.  A real strip on the bench is the final word for
   colour order, and a scope (if one is around) for the half-bit
   widths; neither blocks landing the code against the counter.
5. Audio playing DURING a 256-LED frame and a maximal BITSTREAM: no
   stutter, no dropped console bytes — the entire point of the design;
   this is the demonstration that the interrupts-off route died for.
6. Kill mid-transaction: pin, SM and DMA all return (locktest-style),
   audio untouched.

Gates as ever: make check with new tests/*.bas, cgate 0 diff, fcc,
qemu, ioctlcheck unchanged (no new ioctls — claims only).


## Status — stage 1 BOARD-PROVEN, 2026-08-22

utils/pioouttest **all passed** on COM14 (GP2→GP4 loop + the real
12-LED strip on GP7): 2000-edge bitstream counted EXACTLY 2000; FIN
latched exactly 1000 from a stream running THROUGH the owner's sleep
(the shape that found the swapper); PER 100-cycle latch exactly
200ms; open-collector 199 edges (the counted start-state, see the
test); a WS2812 zero-frame's 576 edges EXACT; five colour frames sent
to the strip; SIGKILL mid-stream left pin+SM+DMA claimable and the
next stream exact.  pioprobe/pioprobe2 (kept in utils/) are the
diagnosis harnesses that cornered the swapper interaction — cold
per-process runs, one ingredient each.

Two lessons paid for and recorded: the swapper correction above, and
**PIO instruction memory is WRITE-ONLY** — pioout_init's original
readback verify panicked a perfectly good load on first boot (the
kernel wedged with tty echo alive and no shell, which is what a
panic's IRQs-on spin looks like from a serial console).  The offset
assert stays; the images are proven by counting their edges on the
wire.

First-run-after-boot note: the very first CIN run of a fresh boot
once read 1991/2000; five repeats and every later run were exact.
Unexplained, watched for, not chased — recorded so a recurrence has
a trail.

Next: stage 3 (the BASIC surface).

## 7. Order of work

1. Kernel `pioout.c` (program images, boot load, claims reserved,
   pinlock claimable()+reset_one() for the PIO/DMA classes) — provable
   from a C test (utils/pioouttest.c) before BASIC exists, against the
   counting inputs.
2. pc3io.h PIO section (register accessors — atomic-only for shared
   registers — and the origin constants).
3. `mmb_pioout.h` + both translators + gates tests.
4. Board acceptance per section 6.
5. Manual: the two commands' entries (with the GP0-7/GP26 rule and
   the timing-exactness note), and the COVERAGE-STATUS NOTES updated —
   WS2812/Bitstream leave the gap list automatically when the
   dispatch learns them.

Not in scope: the two-pin BITSTREAM form; DEVICE SERIALRX/TX (still
category 5); any change to PIO0 (the user-PIO runtime's block) or
PIO2 (the radio's).

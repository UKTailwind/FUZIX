# The remaining kernel surface, and the interrupt problem

Reviewed from `mmb2c/REVIEW-COVERAGE-2026-08-07.md` on 2026-08-09.  That
document marks with **†** every unimplemented MMBasic feature that also
needs new kernel surface.  This gathers all of them into one list, so
the kernel work can be done once rather than a piece at a time, and then
takes on the harder half: the things MMBasic does *between statements*,
on a machine with no threads.

## What the review got wrong about this board

**SPI is not owned by the SD card.**  The review puts SPI in category 4
with "the SPI bus belongs to the SD card".  That is true of the Pico
Computer 2 and untrue here.  On the PC3 the card is on **SPI1** —
`devsdspi.c`: SCK 30, MOSI 31, MISO 28, CS 33 — and not one of those
pins is on the I/O header.  **SPI0** is free, and the header carries
GP0–GP7, which is exactly where the RP2350 can map SPI0 (RX GP0/GP4,
CSn GP1/GP5, SCK GP2/GP6, TX GP3/GP7).  So `SPI` and `SPI2` are
category **2†** — a real, useful, and safe thing to expose, provided
the driver refuses to touch SPI1.

**ADC is half done already.**  `PICOIOC_ADVAL` selectors 1–4 read
GP41–GP44 as 16-bit values today.  `SETPIN AIN/ARAW` and `ADC` do not
need a new mechanism, only a general one: the RP2350B's ADC pins are
GP40–GP47 and the header brings out GP34–GP46.

**ONEWIRE is more feasible than category 5 suggests.**  The objection is
microsecond bit timing under a scheduler.  A OneWire *bit* is ~60–70 µs,
which is a defensible `di()` window in a kernel driver; a whole byte is
not.  So it is 2† *if* it is a kernel driver that disables interrupts
per bit, and category 4 if attempted from userland.  Worth doing only if
DS18B20 temperature (`TEMPR`) is actually wanted.

## The complete kernel surface still to add

Ordered by value for the work.  Every one of these is small on the
kernel side; the MMBasic half is a header of statics per the established
bargain, so a program that does not use it pays nothing.

| # | surface | serves | notes |
|---|---|---|---|
| 1 | `/dev/i2c` | `I2C`, `I2C2`, and every QWIIC sensor | upstream `Kernel/dev/devi2c.c` already exists and only wants `plt_i2c_msg()`; see the arbitration section |
| 2 | PWM ioctl | `PWM`, `SERVO`, `SETPIN … PWM` | frequency + duty per pin; SERVO is PWM with a fixed 20 ms frame, not separate hardware |
| 3 | ADC ioctl | `SETPIN AIN/ARAW`, `ADC` | generalise `PICOIOC_ADVAL`, keep the old selectors working |
| 4 | `/dev/spi` (SPI0 only) | `SPI`, `SPI2` | must refuse SPI1 by construction, not by convention |
| 5 | pin-interrupt registration | `SETPIN INTH/INTL/INT`, and the interrupt model below | GPIO IRQ → `ssig()`; `ssig(ptptr, sig)` is `process.c:794` |
| 6 | periodic timer per process | `SETTICK` | the tick is **200 Hz already** (`TICKSPERSEC 200`), so 5 ms granularity needs no new hardware |
| 7 | key-state ioctl | `KEYDOWN()`, `ON KEY` | the kernel owns the USB keyboard; this is a read of state it already has |
| 8 | block pixel read | `BLIT MEMORY` | the read pairing for `GFXIOC_RECTS`; `GFXIOC_GETPIXEL` is the scalar form |
| 9 | termios exposure | `OPEN "COMn:"` | Fuzix ttys mostly do this; the gap is reaching them from a `.bc` |
| 10 | `MM.HPOS`/`MM.VPOS` | trivial | fold into `GFXIOC_INFO` while the file is open |

Deliberately still refused, and the reasons are unchanged: `PIO` (the
blocks belong to video and audio), `DEFINEFONT` (the kernel owns the
text engine), `MOUSE` (needs a USB-mouse driver and reopens the
pointer/layer question — see `PC3-LAYER-MERGE.md`), raw `PEEK`/`POKE`
(no MMU), and the bit-bang timing family (`IR`, `WS2812`, `BITSTREAM`,
`PULSIN`, `DISTANCE`).

### I2C arbitration, which is the only one with a real design question

The thinking is already done and recorded: the kernel is non-preemptive,
so any driver operation that completes inside one syscall without
sleeping is atomic against every other process, and no lock is needed
for the transaction itself.  Three things remain:

* **The DS3231 sits on this bus at 0x68.**  Linux's answer is the right
  one: the kernel owns that address and `I2C_SLAVE` on it returns
  `EBUSY`, while the rest of the bus stays free.  Do the same.
* **`plt_rtc_secs()` runs inside `timer_interrupt()`** and does a whole
  I2C transaction from interrupt context.  Decided below.
* **Bus hold across syscalls** (MMBasic's no-STOP option) is the one
  case needing a lease.  If exposed: owner in `p_tab`, auto-release on
  exit and exec — the `display_fb_open` pattern already in the tree —
  and a deadline, which MMBasic's `I2C OPEN` timeout already implies.

#### The RTC in interrupt context — DECIDED 2026-08-09

Three options, and the third is the one to build.

1. **Delete it** — return 255 and resync hourly from a userland daemon.
   **Tried, and reverted the same day.**  It works, but the daemon is a
   resident background shell and *that* broke the shutdown path:
   `/etc/rc.reboot` runs `killall`, which is SIGTERM, which a background
   `sh` ignores — so the loop survived, kept the root busy, and
   `remount % ro` failed with `EBUSY`.  The filesystem went down dirty
   and the next boot was crippled.  A long-lived background shell is not
   a safe thing to have on this machine, whatever it is for.
2. **Keep it and lock I2C0 to the kernel**, giving userland I2C1 on
   header pins.  Airtight, but it costs the QWIIC socket — GP20/21, with
   the board's own pullups, and exactly where a user plugs a sensor.
3. **Keep it, and let the poll SKIP when userland holds the bus** —
   chosen.

Option 3 is one test at the top of the poll:

    if (i2c0_user_busy)
            return 255;     /* skip this hour; never wait */

`i2c0_user_busy` is set by the `/dev/i2c` driver around a userland
transaction on I2C0.  Interrupt context then never waits, never takes a
lock and never interleaves — it declines, and `updatetod()` already
understands 255 because that is what a machine with no RTC answers.

It is honest because the poll is *already* best-effort: hourly drift
correction, where missing one costs an hour of crystal drift, i.e.
milliseconds.  And a collision is nearly impossible in the first place —
the kernel is non-preemptive, so the flag is only set while a syscall is
in flight, about 300 µs, against a poll that comes once an hour.

Keeps the drift correction, keeps QWIIC, needs no daemon.  **Build the
flag with the driver that sets it, not before** — a check whose setter
does not exist is untestable, and dead code that looks live is worse
than no code.

## The interrupt problem

`SETTICK`, `ON KEY`, `SETPIN INT*`, `IRETURN`, and everything that
inherits their shape (`MATH PID`, `SENSORFUSION`, sprite collision,
playlist refill) all rely on the interpreter doing work **between
statements**.  Fuzix has no threads.  This is the part worth getting
right, because a wrong answer here is a silent divergence, and this
project's rule is that a silent divergence is worse than an honest gap.

### The shape of the answer

**Signals deliver, generated code dispatches.**  The kernel already has
what is needed to *deliver* an event: `ssig()` sends a signal to a
process from a driver or an interrupt.  What it must not do is run
MMBasic code from a signal handler — a BASIC handler prints, allocates,
draws and opens files, none of which is safe there.

So the signal handler does one thing: set a `volatile sig_atomic_t`
flag.  The generated program checks that flag at controlled points and
calls the BASIC handler from ordinary context.  That is precisely what
the interpreter does between statements, arrived at from the other
direction.

### Why this is affordable — the precedent is already shipped

The obvious objection is cost: a check on every statement in every
program.  But v0.10 shipped exactly this pattern for `ON ERROR` —
**mmbc emits the arithmetic checks only for programs that can trap
them**, and a program with no `ON ERROR` compiles to the machine's own
divide.  The same gate applies here.  A program with no `SETTICK`, no
`ON KEY` and no pin interrupt emits **no polling at all** and pays
nothing.  Only a program that asks for interrupts pays for them, and
it asked.

For scale: the native stack guard added three instructions to every
function entry and measured **0.76%** on a loop doing nothing but
calling.  A flag test at loop back-edges is the same order.

### Where the check goes — the one real decision

Three candidates, and they trade latency against cost:

1. **Every statement.**  Closest to MMBasic, and the honest default if
   the cost turns out not to matter.
2. **Loop back-edges and before each call.**  Much cheaper, and catches
   everything that can *spin* — which is everything that can starve a
   handler indefinitely.
3. **Back-edges only.**  Cheapest; a long call chain defers the handler.

Recommendation: build (2), measure it against (1) with the A/B method
in `pc3-benchmark-method`, and keep (1) if it is inside noise.  The
handler latency is then bounded by the longest straight-line run
between check points, which is a statement of fact a manual can make.

### What must be said out loud, not discovered

* **Latency is bounded by the check interval, not the timer.**  A
  `SETTICK 10` will not fire every 10 ms inside a long computation.
  MMBasic has the same property; ours needs measuring and documenting.
* **The finest honest period is the tick: 5 ms.**  `TICKSPERSEC` is
  200.  `SETTICK 1` cannot be met and should say so rather than silently
  rounding — a divergence the user cannot see is the worst kind.
* **Handlers do not nest.**  One handler at a time, the flag cleared
  before it runs, as the interpreter does.
* **Blocking I/O defers handlers** unless the check is also inside the
  runtime's own wait loops (`INPUT`, file reads, `PAUSE`).  `PAUSE`
  already yields; the others need the same treatment or an honest note.
* **Sub-tick work stays refused.**  IR decode and WS2812 are not made
  possible by any of this, and should keep saying so by name.

## Suggested order

One at a time, each board-verified before the next, per the standing
rule.  The first three are the ones that unlock real programs:

1. **I2C** — biggest single win; QWIIC is on the front of the board and
   every hobby sensor speaks it.  Carries the `i2c0_user_busy` flag with
   it, per the decision above.
2. **PWM + SERVO** — small, self-contained, immediately useful.
3. **ADC generalisation** — mostly exists; finishing it is cheap.
4. **The interrupt machinery** (5, 6, 7 together) — they share one
   mechanism, so build the flag-and-dispatch model once and hang
   `SETTICK`, `ON KEY` and pin interrupts off it.
5. **SPI0** — real but less demanded than I2C; do it once the `/dev/i2c`
   shape has proven itself, and copy that shape.
6. The small ones (block pixel read, termios, `MM.HPOS`) as they are
   wanted.

Doing 1–5 does genuinely put the kernel to bed for MMBasic purposes:
everything left in the review that needs kernel surface is then either
implemented or refused for a reason that will not change.

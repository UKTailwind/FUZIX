# Pulsin( and Distance( — measuring a pulse on a machine that preempts

**Status: designed 2026-08-23, on the board measurement below.**

## What the reference does

`Pulsin(pin, polarity [, t1 [, t2]])` → INTEGER microseconds, or **-1**
on timeout (`misc/External.c:2548-2596`). The pin must already be
`SETPIN pin, DIN` or it raises `Pin %/| is not an input`. `polarity`
non-zero measures a HIGH pulse, zero a LOW one. `t1` and `t2` default to
100000 µs and are `getint(..., 5, 10000000)` — 5 µs to 10 seconds; `t2`
defaults to `t1` when only `t1` is given.

The shape, for polarity non-zero, is three spins off one microsecond
clock that is zeroed once at the top:

    while pin is HIGH   : give up if the clock passes t1   (finish the pulse in progress)
    while pin is LOW    : give up if the clock passes t1   (wait for the rising edge)
    zero the clock
    while pin is HIGH   : give up if the clock passes t2   (measure it)

so `t1` bounds the two waits TOGETHER, not each — the clock is not
re-zeroed between them. Polarity zero is the same with the levels
swapped. Replicate exactly, including that.

`Distance(trig [, echo])` → FLOAT centimetres, **-1** no echo, **-2** no
acknowledgement pulse (`External.c:2303-2352`). One pin means a 3-pin
device where trigger and echo are the same wire. The sequence is: echo
to input with pull-up; trigger low, then output; trigger high for 20 µs;
low; 50 µs; echo re-configured as input with pull-up (for the 3-pin
case); 50 µs; then wait for the acknowledgement pulse to start (50 ms
limit) and end (100 ms limit), then time the echo pulse with a 38 ms
limit and divide by 58.0.

## The measurement that decides the design

`utils/spingap.c` reads the microsecond clock in the tightest loop there
is and reports the gaps. On the board, 2026-08-23 (kernel v0.20):

| | worst gap | 10–24 µs gaps | of the wall clock |
|---|---|---|---|
| alone | **471 µs** (once in 5 s) | 1,723 in 5 s (~345/s) | 0.47 % |
| with one other runnable process | **500,446 µs** | ~1,020 in 5 s | 46 % |

The loop itself samples every ~140 ns, so nothing there is us.

Two separate facts, and they point in different directions:

* **Alone**, the interruption is the 5 ms system tick, whose body holds
  `di()` (`devices.c:83-139`) — 14–18 µs, about 345 times a second. A
  1 ms pulse crosses one about a third of the time, and the error when
  it does is up to 18 µs, or 1.8 %. There is also a rare ~500 µs event,
  once in five seconds.
* **With any second runnable process**, the timeslice is **half a
  second**. A busy-wait measurement is not noisy then, it is nonsense —
  and nothing in a BASIC program can tell that it happened.

The reference does not have this problem to the same degree: a PicoMite
runs one program, and its tick body is short.

## So: the kernel timestamps the edges, and the program can be as late as it likes

The insight the measurement gives is that **the accuracy of a
measurement and the scheduling of the program that asked for it can be
separated**. If the edge times are recorded by the GPIO interrupt, the
process may be descheduled for half a second and still read back an
exact pulse width when it returns.

That hardware is already here, shipped and proven: `countpin.c` owns the
GPIO interrupt at priority 0, RAM-resident (`relocate_gpio_irq_to_ram.
cmake`), for GP4–GP7 — the counting inputs. The remaining error is the
interrupt latency, which is bounded by the same `di()` window as above:
sub-microsecond normally, up to ~18 µs if the tick is running, whatever
else the machine is doing. That is two orders better than the busy-wait
under load, and it never lies by 500 ms.

### The shape

* A new mode in `countpin.c` alongside FIN/CIN/PER: **CAPTURE**. Arming
  it enables both edges on the pin and clears a small ring of
  (timestamp, level) pairs in that pin's struct. The IRQ callback
  already runs per pin; capture adds a `pc3_us64()` read and a ring
  write.
* `GPIOC_CNT_CAP` arms and disarms; `GPIOC_CNT_CAPRD` copies the ring
  out with a sequence number, so userland can tell "nothing new yet"
  from "I missed some".
* Userland (`mmb_pulse.h`) implements MMBasic's state machine over the
  ring rather than over the pin: the same three phases, the same
  timeouts read from the same microsecond clock, and the same -1. It
  sleeps between polls rather than spinning, because it no longer needs
  to be awake at the moment of the edge.
* `Distance(` drives the trigger with the existing pin writes and then
  measures the echo through the same capture path. Its three limits (50
  ms, 100 ms, 38 ms) are milliseconds, so polling is easily fast enough.

### What this costs, and what it refuses

**GP4–GP7 only**, the pins the counting hardware already owns —
`Pulsin(` and `Distance(` on any other pin are refused by name, with a
message that says which pins do work. That is the same bargain the
counting inputs made (`PLAN-count.md`: fixed pins, `OPTION COUNT`
refused) and the user has accepted it once already. A program that
wants another pin gets an honest error rather than a number that is
right when the machine is idle and wrong when it is not.

**One capture pin at a time per process**, and the pin must be `SETPIN
pin, DIN` first, exactly as the reference requires.

**The alternative was considered and rejected**: a plain userland
busy-wait, as the reference has it. Alone it is good to ~2 % on a 1 ms
pulse; with a second process runnable it is wrong by up to half a
second, silently. `different is worse than missing` decides it. (The
busy-wait would also have needed a guard that watches its own gaps and
returns -1 when interrupted — at which point it is a worse version of
this with more explaining to do.)

**Not the PWM slice**: RP2350's PWM can count high cycles on a B input
without any interrupt at all, which is exact — but only ODD GPIOs can be
a B input, and the 16-bit counter cannot reach Pulsin's 10-second range
without an overflow interrupt anyway. It would restrict the pins further
AND still need the IRQ.

## Stages

1. `utils/spingap.c` — **DONE**, the numbers above.
2. Kernel: CAPTURE mode in `countpin.c`, the two ioctls, the pinlock
   reset path — **DONE and board-proven**, see below.
3. `mmb_pulsin.h`: `mmg_pulsin()` and `mmg_distance()` over the ring —
   **DONE**, host gates green.
4. Both emitters: `Pulsin(` and `Distance(` with the reference's
   argument counts, ranges and errors; `tests/pulsin.bas` — **DONE**,
   byte-identical by cgate.
5. Board acceptance from BASIC — `samples/pulsintest.bas` against the
   board's own PWM. **DONE**, and the numbers are below.
6. `Distance(` against a real HC-SR04 — **DONE, board-proven
   2026-08-23** once the sensor was answering (the first attempt is
   below, and was the bench rather than the software).

## What the board said

The bench link is GP3 (PWM out) to GP5 (capture), on COM17.
`utils/captest.c` drives a known pulse and measures it through the
ring:

| | asked | min | max | mean |
|---|---|---|---|---|
| idle | 250 us | 250 | 250 | 250 |
| **with `spingap` spinning flat out** | 250 us | **250** | **251** | **250** |

That second row is the design's whole argument, measured: the same
pulse, measured by a program the scheduler is taking off the CPU for
half a second at a time, still reads within a microsecond. A busy-wait
would have been wrong by up to 500,000.

And from BASIC, `samples/pulsintest.bas` through the card's own `cc`:

| | asked | read |
|---|---|---|
| idle | 250 us high | 249, 249, 250, 249, 249 |
| idle | 750 us low | 750, 750, 751 |
| idle | 1000 us high | 1000, 1000, 999 |
| **with `spingap` spinning** | 250 / 750 / 1000 | **250 x5, 750 x3, 1000 x3** |
| PWM off | — | -1, -1 |

The +/-1 us idle is the microsecond timer's own quantisation against
PWM edges that do not land on its ticks. Under load the readings are if
anything MORE uniform, because the program is descheduled into whole
pulses rather than catching them mid-poll - which is the design saying
what it was built to say.

## `Distance(` on a real HC-SR04

Trigger on GP1, echo on GP7, `samples/hcsr04.bas` through the card's
own `cc`:

| reflector at | read | with `spingap` spinning |
|---|---|---|
| 15 cm | 15.0, 15.0, 14.6, 15.0 ... (15 readings) | — |
| 20 cm | **21.5 cm x15, not a flicker** | **21.5 cm x15, identical** |

It tracks the move exactly - the raw probe measured the same line at
822-871 us with the reflector at 15 cm, and 21.5 cm is 1247 us - with a
consistent offset of about 1.5 cm at this range, which is the sensor
and the geometry rather than the arithmetic.  58 us per centimetre is
MMBasic's own divisor, so a PicoMite reading this waveform prints the
same number.

Fifteen identical readings under a process spinning flat out is the
same statement `Pulsin(` makes one layer down, in the units a user
cares about: **the measurement does not care what else the machine is
doing.**

## The first attempt, and what the wire was doing then

`Distance(GP1, GP7)` returned **0.1 cm**, over and over. That is
3 us / 58, and 3 us is what the echo line does. `utils/hcprobe.c`
watches the same pulse two independent ways — the kernel's edge capture
and a tight CPU sampling loop — and they agree to the microsecond:

    echo reads 0 before the trigger
    CPU sampled 2 transitions:      449 us -> 1     452 us -> 0
    the interrupt recorded 2:       449 us -> 1 (events 8)
                                    452 us -> 0 (events 4)

Identical with a pull-up, floating, and with a pull-down. So the module
WAS answering the trigger — 450 us is exactly an HC-SR04's echo latency
— and then held ECHO high for three microseconds instead of the
hundreds a range would take. **The software was reading the wire
correctly; the wire was the problem**, and the bench fixed it: the same
program on the same code then read 15 cm and 21.5 cm.

Keep the shape of this rather than the details. A number that is
plausible in FORM (0.1 cm is a distance) and impossible in VALUE is the
one to distrust, and the way to settle it is a second, independent
measurement of the same physical event — not a closer reading of the
code.

**A method trap worth keeping**: the first sampling run reported a
224 us pulse and disagreed with the interrupt, which sent me looking
for a kernel bug that was not there. The sampler was printing INSIDE
its loop, and a line of console output at 115200 takes ~1.6 ms - so it
missed the real fall and reported the next one it happened to catch.
Sample into memory, print afterwards. Two mechanisms that disagree are
worth trusting only once both are innocent of their own instrument.

One trap paid for on the way: the first run read 100 us for a 250 us
request, and the capture was not at fault — this machine's `clk_sys` is
375 MHz, so the PWM counter at divider 250 runs at 1.5 MHz, not the
0.6 MHz a 150 MHz part would give. `cnttest.c` had the right constant
all along. A test that generates its own signal can be exactly wrong
about what it generated, and the measurement will agree with the
hardware rather than with the intention.

## Acceptance

* A PWM pulse of a known width reads back within the interrupt latency,
  and **reads the same while a second process spins flat out** — that
  second run is the whole point of the design.
* `Pulsin` returns -1 on a silent pin after t1, and after t2 on a pin
  that goes active and stays there.
* `Distance` returns -2 with nothing connected.

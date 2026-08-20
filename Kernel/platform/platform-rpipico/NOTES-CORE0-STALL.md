# CORE0 STALLED — open, undiagnosed

2026-08-20. Twice during the networking work the machine stopped dead
and core1 reported `CORE0 STALLED`. Not reproducible on demand. This
note records the evidence and what it rules in and out, so that whoever
picks it up does not start from nothing.

**Status: open. Not diagnosed, not fixed, and not understood.** What
follows separates what was seen from what is inferred, because the
inference is thin.

## The three captures

Verbatim, and they are the whole primary evidence:

    [CORE0 STALLED phase=0 beat=0000563E nsys=000001FA]   (repeated, identical)
    [CORE0 STALLED phase=0 beat=0000A7D7 nsys=000001FA]
    [CORE0 STALLED phase=0 beat=0000E958 nsys=000002EA]   (repeated, identical)

1. During `time ./tlsget 104.20.23.154 example.com /`, on a kernel
   carrying a 16K dedicated pump stack, before TLS worked. Uptime about
   110s.
2. Shortly after a successful `wifi -f`, running a shell line that made
   a directory, wrote a file, copied another and listed the result.
   Uptime about 215s.
3. On the v0.18 release kernel, during `mkdir -p /tmp/www` — as
   ordinary a system call as exists — a few seconds after `wifi -f`,
   `ntpdate`, `tlsca /etc/ca.pem` and a successful `tlsget`. Uptime
   about 298s.

**The third is the most informative and the most alarming.** The work
in progress was a `mkdir`. Nothing heavy, nothing to do with the
network, and the console was nearly idle — so "heavy console output
plus networking", which the first two suggested, is not the condition.
What all three share is that **a TLS or Wi-Fi operation had happened
shortly before**.

Once it happens, it stays: core1 repeats the line every two seconds
indefinitely, with every field frozen. The machine does not recover.

## Reading the fields

From `display.c`'s `disp_core1_watch()`, which fires when
`pc3_tickbeat` has not moved for ~120 frames (about two seconds) and
then repeats every two seconds for as long as the condition holds.

**`phase=0` is the loudest fact.** `pc3_tickphase` is set at each step
of `timer_tick_cb()` and set back to 0 on the way out, and the comment
there says exactly what 0 means:

> 0 = left the tick cleanly. If the watchdog reports a stall at phase 0
> then the body is not the problem at all: the tick finished and was
> simply never called again, which puts the fault in the alarm pool or
> in what PendSV went off and did.

So the tick did not hang inside itself. It completed and was never
re-entered.

**`beat`** is the tick counter at the freeze: 0x563E = 22,078 and
0xA7D7 = 42,967, so about 110 and 215 seconds of uptime at 200Hz.
Different in the two events; nothing suggests a fixed time.

**`nsys`** is `pc3_syscount`, incremented on entry to
`syscall_handler()`. Its purpose is precisely to answer "is core0 alive
at all?" — climbing while the tick is dead means core0 is running
userland and only the timer stopped; frozen means core0 is not
executing. In the first event it was **frozen across all five repeats**,
which says core0 was not taking syscalls for at least ten seconds.

## What `nsys` says, and a wrong turn worth recording

The first two events both froze at `nsys=0x1FA` (506) and that looked
like the strongest lead in the case — the same syscall count at the
moment of death, in two runs of different length and different work,
suggesting a *reproducible point* in a fixed startup sequence.

**The third event killed that.** It froze at 0x2EA (746). Three
samples, two values, and the two that matched were the two runs whose
scripts were most alike. So the count is not a fixed point; it is just
where that particular run had got to.

What the counter still tells us is what it was built to tell us. `bl
syscall_handler` in `tricks.S` is the only syscall path, so it counts
every system call, and in events 1 and 3 it was **frozen across every
repeat** — ten seconds and several minutes respectively. Core0 was not
taking system calls. Combined with `phase=0`, the picture is a core0
that has stopped executing rather than a timer that has stopped
firing.

Recording the wrong turn because it is the kind that wastes a day: two
matching numbers out of two samples is not a pattern, and the third
sample was cheap.

## What this is NOT

**Not the 2026 tick re-arm freeze.** That one was a hand-rolled
`hardware_alarm_set_target` left unarmed after a missed deadline. It
was fixed by moving the tick to the SDK's alarm pool, which keeps an
ordered list and re-fires immediately on a miss — see the note in
`CMakeLists.txt`, which states that the pool "cannot end up unarmed".
`phase=0` says the tick was never called again, so either that claim is
too strong, or core0 stopped executing and the pool never got the
chance. The second is more likely and is what `nsys` was supposed to
settle.

**Not the console wedge** (`NOTES-console-wedge.md`). That has a
different signature — `[u0 ...]` from `rawuart_report`, with the tick
provably ALIVE (`tk` counting in every capture). Here the tick is dead.
They may still share a cause, but they are not the same event and
should not be merged in the analysis.

**Not the TLS stack overflow.** That was diagnosed and fixed
separately: a handshake needs 2,460 bytes of kernel stack and had
1,156. The first stall happened on a kernel that had 16K available, so
it was not short of stack.

## Circumstances worth noting

**All three followed a TLS or Wi-Fi operation within a minute or so.**
That is the only condition common to all of them. It is three samples,
but the third rules out the alternative the first two suggested: it
happened during a `mkdir` on a quiet console, so it is not about
console load, and it is not about being inside network code at the
time — the network work had already finished.

That points at something the radio, lwIP or mbedtls *leaves behind* —
a timer, a DMA channel, an interrupt, a piece of allocated PSRAM —
rather than at something being executed when the machine dies.

Candidates, in no particular order and none tested:

- **core0 halted outright.** A fault handler that ran, printed its dump
  somewhere invisible, and stopped. `picofrog` died this way for a week
  because the panic text went to a framebuffer that was not on screen.
  Worth checking whether the fault path can be reached with the console
  in a state where its output goes nowhere.
- **A bus stall.** The QMI serves both PSRAM and flash, lwIP's heap is
  in PSRAM, and the scanout is reading continuously. A stalled core0
  with a live core1 is consistent with core0 waiting on a bus that
  core1 is not.
- **An interrupts-off deadlock** in a path only networking reaches.
- **The alarm pool** genuinely losing the tick, which would contradict
  the reasoning in `CMakeLists.txt` and would matter well beyond
  networking.

## How to make progress

In the order that costs least:

1. **Try to reproduce it deliberately.** Three events in one day, all
   within a minute of network activity, is close to a recipe already:
   boot, `wifi -f`, `tlsca`, a `tlsget`, then a loop doing trivial
   filesystem work and printing nothing. If that dies within a few
   minutes, everything below becomes cheap. A reproduction is worth
   more than any amount of the rest of this list.
2. **Make core1 say more.** It is the only thing still running, it can
   already write the UART directly, and it is not constrained by what
   core0 is doing. Print core0's stacked PC — core1 cannot read core0's
   registers directly, but core0 can leave a breadcrumb: store the
   syscall number and a timestamp somewhere core1 can read, and have
   core1 report the last one and its age. That turns "core0 is stuck"
   into "core0 is stuck in this call, and has been for N ms".
3. **Watch the QMI.** If a bus stall is suspected, core1 can sample
   whether it is itself seeing PSRAM latency at the moment core0 dies.
4. **Try to provoke it.** Networking plus saturated console output is
   the only common factor. A loop doing `htget` of something large
   while a second process writes continuously to the console would be
   the cheapest attempt at reproduction, and a reproduction is worth
   more than any amount of the above.

## What it costs today

Three times in one day, and the third was on the v0.18 release kernel
doing nothing more than `mkdir` a few seconds after a TLS fetch. The
first two could be written off as a test harness driving the console
flat out; the third cannot.

**This affects normal use of networking.** A machine that has joined a
network and fetched something over TLS may lock up hard some seconds or
minutes later, with no output, needing the reset button. It does not
appear to corrupt the filesystem — the card fsck'd clean each time —
but everything unsaved is lost.

It is not known whether a machine that never touches the network is
affected. Nothing in months of use before this work behaved this way,
which is suggestive but not evidence.

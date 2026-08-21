# CORE0 STALLED — diagnosed and fixed, 2026-08-21

A deadlock in the console output path. `lineedit`'s `put()` spun on
`tty_writeready()`, and every `lineedit` path runs inside
`tty_interrupt()`, which `timer_tick_cb` calls with `di()` held. Once
the 256-byte transmit ring filled, its only two drainers were the
transmit interrupt — masked, so it could never run — and
`rawuart_tx_poll()`, which is a *later step of the very tick now
spinning*. An echo or redraw burst bigger than the ring hung core0 for
good.

Fixed in `lineedit.c` (commit `abe9b1b7f`): drop the pre-check spin and
call `tty_putc(1, c)` directly. `rawuart_putc` already handles a full
ring in both contexts — in thread context it waits on the interrupt,
and when masked it pumps the ring into the FIFO itself. The worst case
degrades to a burst clocked out at wire speed, or a BEL on an over-long
line. Never a lockup.

**Status: believed fixed, watch for recurrence.** One piece of evidence
below does not fit cleanly and is worth knowing about if it comes back.

## The proof

Reproduced deterministically on a PC2: a ~100-character history line,
up-arrow, then down-arrow — whose `erase_line` emits about 300 rubouts
in a single tick.

The `[u0 ...]` report is what sealed it:

    en=1 pd=1 ac=0 pm=1 ri=0020 (TXRIS)   ring full, h=081 t=082

The transmit interrupt enabled, pending, and asserting — and
undeliverable, because PRIMASK really was held.

## What this corrects

**`pm=1` was the clue, not noise.** `NOTES-console-wedge.md` reads
"PRIMASK set (normal - the report prints from the tick)" and this note
previously repeated that reasoning. For this failure it was exactly
backwards: PRIMASK being set was the fault, not an artefact of where the
report is printed from. A field that is *usually* uninteresting is not
the same as one that can be skipped.

**Three triggers, one mechanism.** They looked like different faults and
are not — each is a large echo or redraw burst generated inside
`tty_interrupt()` under the tick's mask:

- keyboard and serial together — more characters drained per tick;
- a fast stream into `cat` — `lineedit` echoes every byte in cooked mode;
- long concatenated command lines — one-tick overflow.

**Wi-Fi was an aggravator, not a cause**, which matches the prior the
author held throughout and which this note originally recorded as
unproven. More busy and masked windows elsewhere mean the ring is more
often full when a burst lands. Anyone who had started the hunt inside
lwIP or mbedtls would have been in the wrong file.

**Why MMBasic never locks up here.** It runs echo and line editing in
thread context with interrupts on, so its identical full-ring spin
always has a live drainer. The worst it suffers is dropped receive
bytes. That is the shape the fix restores: pace or drop, never wedge.

## The loose end

The three captures in this note all reported **`phase=0`**. The
reproduction and the fix are **`phase=2`**.

`pc3_tickphase` is set to 2 immediately before `tty_interrupt()` and
back to 0 on the way out of the tick, so a hang inside `lineedit` under
the tick reports 2 — as the reproduction did. `phase=0` says the tick
completed and was never re-entered, which is a different statement.

That may simply be a sampling artefact, or those three may have been the
same deadlock reached by a path that leaves the phase at 0. But it is
the one fact here that the mechanism does not obviously account for, so:
**if `CORE0 STALLED` is ever seen again, look at the phase first.** A
`phase=2` recurrence means the fix is incomplete. A `phase=0` recurrence
means there is a second fault and this note was closed too early.

The captures, for that comparison:

    [CORE0 STALLED phase=0 beat=0000563E nsys=000001FA]   during tlsget
    [CORE0 STALLED phase=0 beat=0000A7D7 nsys=000001FA]   after wifi -f
    [CORE0 STALLED phase=0 beat=0000E958 nsys=000002EA]   during mkdir

## Method, worth keeping

Two things in this hunt were worth more than the reasoning:

**A deterministic reproduction ended it.** Three captures over a day of
hard use produced a plausible story that was wrong in its central claim
(that networking was implicated). One keystroke sequence that fails
every time produced the mechanism in an afternoon.

**Two matching numbers are not a pattern.** The first two captures both
froze at `nsys=0x1FA` and this note called that "the strongest lead in
the case", reasoning at length about a reproducible point in a fixed
startup sequence. The third capture read `0x2EA` and the whole edifice
went. The two that matched were simply the two runs whose scripts were
most alike.

# Console wedge during streamed input - open, intermittent

2026-08-06.  Twice during uusend transfers the serial console died in
both directions mid-stream; the machine did NOT panic.  Status: not
yet reproducible on demand; evidence and suspects recorded here.

## The two events

Both printed the rawuart stall report to the screen as the last line:

    [u0 tk=00187F26 en=1 pd=1 ac=0 pm=1 bp=00 ri=0470 ...  t=004]   (4.5h uptime)
    [u0 tk=00005650 en=1 pd=1 ac=0 pm=1 bp=00 ri=0470 h=0c8 t=0c9]  (~30s after boot)

Decode: ticks alive at report time; uart IRQ enabled+pending, handler
not active; PRIMASK set - **and see the correction below, because that
was read as normal and was not**;
ri=0470 = RX+TX+RT raw PLUS **OE: receive overrun**; second event's tx
ring full (h+1==t).  Above the report: only echoed transfer payload -
and console_putc paints the SCREEN before the uart (console.c), so a
panic would have left text.  **No panic fired; fsck found only
crash-instant damage (free count off by one).  FS32 corruption is
exonerated.**

## What does NOT trigger it (all tested 2026-08-06, gap 0, full rate)

* The identical 78K uuencoded stream into `cat > /dev/null`, twice
  (devtools/dnull.py - keep it; it is the discriminator).
* The same stream as a real file write, fresh boot.
* ~490KB of rm churn followed immediately by the same transfer.

Five other transfers up to 195K succeeded the same day.  The two
failures followed metadata churn (six-file rm; a chmod+benchmark
session), but replicating that did not reproduce.

## Suspects, in order

1. **The stall detector can false-positive by aliasing.**  At 115200
   the echo stream moves exactly 11,520 chars/s = 45.0 laps of the
   256-byte tx ring per second: a sampler comparing head/tail
   snapshots 1s apart sees identical values on a saturated-but-FLOWING
   ring.  Fix: compare a monotonic pump COUNTER, not positions.
2. **The report itself prints ~80 chars to the screen from inside the
   tick.**  If that scrolls the framebuffer it is a multi-millisecond
   PRIMASK-held window in the middle of a saturated stream - OE is
   guaranteed (32-byte FIFO = 2.8ms at wire speed) and every rx
   interrupt in the window is coalesced into one.  Whether the
   subsequent cascade can permanently strand the pending IRQ is the
   open question; en=1 pd=1 with ticks running should redeliver, so
   the terminal state is not yet explained by static reading.
3. A long di() window in the fs write path (sync daemon flush landing
   mid-stream) shrinking the rx margin - would explain the timing
   correlation with write-heavy sessions and the intermittency.

## Recovery

Reset; at `bootdev:` answer hdb2 then root.  fsck will want two boots
(repair, then "modified - reboot").  Expect a one-count free-inode fix
and a free-list rebuild; nothing structural has ever been found after
these.

## Instrumentation for next time

rawuart_report already latches once - extend it to also dump
rxring lost/got and a tx pump counter, and log a SECOND report 5s
later: whether the counters moved between the two reports is the
single fact that separates "saturated and flowing" (false stall,
suspect 1) from "IRQ genuinely dead" (real wedge, mechanism unknown).

## 2026-08-06 late: hardened, wedge eliminated by construction

A third wedge (~36s after boot, same [u0 ...] signature) settled it.
Two facts sharpened first: the report fires from rawuart_ready after
~2M process-context spins WITH INTERRUPTS ENABLED - so the stranded
pending bit is real, not sampler aliasing - and suspect 1 is
withdrawn.  And plt_rtc_secs' I2C-under-di() only runs HOURLY
(RTC_SYNC_SECS), so it fits the 4.5h wedge but not the ~30s ones;
the ~30s masked window remains unidentified (update daemon flush is
the open candidate).

Rather than keep chasing the stranding mechanism, rawuart.c now
survives it (kernel build Aug 6 2026 19:21):

* the ISR DRAINS both directions instead of taking one character per
  entry - one pending bit covers a whole backlog;
* the tick is a full polled rescue: it drains the rx FIFO into the
  ring, pumps the tx ring, and clears the error latches every 5ms.
  The tick provably survived all three wedges (tk counted in every
  report), so lost interrupt delivery now degrades the console to
  polled (32 bytes / 5ms each way) instead of killing it.

Board-verified: full-rate transfers no longer wedge.  Residual, seen
once: a multi-ms masked window still LOSES characters (a 78,820-byte
transfer arrived 45 short; uusend printed its echo-timeout warning
and the retry was clean).  The stranding mechanism itself is still
unexplained - the polled rescue makes it harmless, not understood.

Remaining refinements, host side first: uusend should verify the
echo per line and RESEND the damaged line instead of just warning;
the second-report instrumentation above is still worth having the
day the mechanism is hunted for real.

## 2026-08-21: the mechanism, and a correction to this note

Found, and it was a deadlock in the console OUTPUT path rather than
anything stranding an interrupt.  `lineedit`'s `put()` spun on
`tty_writeready()`, and every `lineedit` path runs inside
`tty_interrupt()`, which `timer_tick_cb` calls with `di()` held.  Once
the 256-byte tx ring filled, its only two drainers were the tx
interrupt - masked, so it could never run - and `rawuart_tx_poll()`,
which is a LATER STEP OF THE VERY TICK now spinning.  An echo or redraw
burst bigger than the ring hung core0 for good.  Fixed in `lineedit.c`
(`abe9b1b7f`); see `NOTES-CORE0-STALL.md` for the full account.

**This note read `pm=1` as normal - "the report prints from the tick" -
and that was exactly backwards.**  PRIMASK being set WAS the fault: the
tx interrupt was enabled, pending and asserting, and undeliverable
because the mask really was held.  The field was dismissed as an
artefact of where the report is printed from, and it was the clue.

A field that is usually uninteresting is not the same as one that can
be skipped, and writing "normal" beside it in a note is how a reader
two months later skips it too.

The polled rescue described above is still right and still wanted - it
is what keeps a genuinely lost interrupt from killing the console - but
it was never going to help here, because the tick that would do the
rescuing was the thing that was stuck.

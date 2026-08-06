# Console wedge during streamed input - open, intermittent

2026-08-06.  Twice during uusend transfers the serial console died in
both directions mid-stream; the machine did NOT panic.  Status: not
yet reproducible on demand; evidence and suspects recorded here.

## The two events

Both printed the rawuart stall report to the screen as the last line:

    [u0 tk=00187F26 en=1 pd=1 ac=0 pm=1 bp=00 ri=0470 ...  t=004]   (4.5h uptime)
    [u0 tk=00005650 en=1 pd=1 ac=0 pm=1 bp=00 ri=0470 h=0c8 t=0c9]  (~30s after boot)

Decode: ticks alive at report time; uart IRQ enabled+pending, handler
not active; PRIMASK set (normal - the report prints from the tick);
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

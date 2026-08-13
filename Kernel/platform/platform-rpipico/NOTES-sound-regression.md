# RESOLVED: the PLAY SOUND saga (2026-08-13)

Three separate faults stacked on top of each other, which is why every
single fix "didn't work" until the last one landed.  Board-verified by
ear at the end: soundab, playdemo, tonequiet all clean.

1. **The kernel FIFO delivered zeros to reopening writers.**  A pipe's
   read/write positions lived in each fd's own offset, so a client
   that opens-writes-closes per message (mm_play_send) wrote every
   record over the first one while the reader walked ahead into
   never-written blocks (`mapcalc` → NULLBLK → `zerobuf()`).  Fixed:
   the positions are the PIPE's, in the in-core inode
   (cinode.c_pipe_roff/woff), reset when c_refs comes up from zero.
   fifotest leg 5 (a fresh writer per record) is the regression test.
   Upstream-relevant: mainline FUZIX has the same structure.

2. **The machine periodically stalls its processes for ~150 ms**, on a
   slow heartbeat (~every 1.2 s under our tests).  pcmpace's cushion
   scan pinned it: 512-frame chunks at a 16K (93 ms) queue target =
   168 underruns in 5 s; 24K = 17; **32K (186 ms) = zero**.  The
   audio IRQ/DMA ride straight through, so a deep enough cushion is a
   complete cure; MMBasic's 93 ms is not deep enough HERE.  playsnd
   ships with 32K, costing ~0.2 s of note-change latency.  **The
   stall itself is an open hunt**: `pcmpace seconds chunk target
   sleep` is the measuring instrument; suspects worth timestamping
   are flash/QMI housekeeping and anything on a ~1 s timer.  Finding
   and fixing it buys the latency back.

3. **My own diagnostics were a fourth fault.**  The daemon's per-record
   stderr logging blocked on the 132-byte tty queue and scrolled the
   mirrored display (~35 ms per line, all processes frozen - the rate
   every uusend runs at confirms it), which starved the very cushion
   being measured.  Instrument quietly or instrument kernel-side.

Also shipped in the same round: polyBLEP band-limited square/saw in
playsnd (same treatment as the kernel synth and the PicoMite engine);
the kind file (/tmp/.playkind) so a program born after the daemon can
adopt it instead of raising "Sound output in use"; playsnd t/d test
modes; sndclient (one record, no BASIC) and sndharness (the renderer
on the host, writing a WAV - it proved the arithmetic perfect while
the board pulsed, which moved the hunt to the feed).

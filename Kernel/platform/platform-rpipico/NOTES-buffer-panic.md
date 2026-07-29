# "panic: no free buffers" — briefing note

2026-07-29. **OPEN.** This is the live kernel problem; the inode double
free that preceded it is fixed (`NOTES-inode-freelist.md`).

## The symptom

    panic: no free buffers

The board dies. On the crashes seen so far the HDMI output goes first,
which is what it looks like from the front, but that is a consequence
and not a clue - see "eliminated" below.

## Why it took four crashes to see the message

It was going into bytes that were being thrown away. `uusend.py` waits
for each line to echo back as its only flow control, and it *counted*
those bytes without keeping them. Every word the board said during a
transfer went into that count.

`uusend.py` now logs everything the board sends to `%TEMP%\uusend.log`
and prints the tail on an echo timeout. **Read that log first.** It is
how the panic was finally caught.

## What it means

`freebuf()` in `devio.c` panics when every buffer in the pool is busy:

    for (bp = bufpool; bp < bufpool_end; ++bp)
        if (bufclock - bp->bf_time >= oldtime && !bisbusy(bp))
            oldest = bp;
    if (!oldest)
        panic(PANIC_NOFREEB);

`NBUFS` is 20 (`config.h`).

**The machine was idle when it died.** That is the important fact: an
idle machine cannot be under buffer pressure, so twenty pinned buffers
mean something finished and did not release them. This is a leak.

**Do not just raise NBUFS.** It would turn a crash in an hour into a
crash in three and lose the signal.

## Instrumentation in place

`freebuf()` now dumps the whole pool before panicking - index, device,
block, busy count, dirty flag. "No free buffers" on its own cannot
distinguish a leak of one buffer per operation from real pressure; the
list names the holder. This is the same trick that cracked the inode
bug, where recording *which code path* put an entry on the free list
was what finally identified it.

Next crash: read the dump, and read `%TEMP%\uusend.log`.

## Eliminated — do not re-investigate

* **Every `bread`/`brelse` pair** in `filesys.c`, `inode.c` and
  `blk512.c`, error paths included. All balanced. `breadi`, `bwritei`
  and `bmap` in particular.
* **The compiler work.** The leak shows with the machine idle, and the
  filesystem changes made the same day are accounted for below.
* **Flash writes.** Worth reading the reasoning in
  `git show 25f1e4e50` before going near this: the kernel is
  `PICO_COPY_TO_RAM`, `.text` is 78K at 0x20000110, `.rodata` is empty
  and the vector table is `ram_vector_table` at 0x20000000. **Core1
  never touches XIP**, so a flash write needs only core0's interrupts
  masked. A real hazard was found there and fixed - boot2 re-runs
  inside the flash routines and leaves QMI M0 at CLKDIV=2, and they
  invalidate the XIP cache that PSRAM (and therefore swap) is cached
  through - but it is not this.
* **Core1 / the display.** Its code is in RAM, its data is non-const
  and so in RAM, its stack is an assigned array in SCRATCH_X with a
  sentinel that `display_stack_check()` tests. The blank screen is a
  symptom of the machine dying, not a cause.

## Noted while looking, not a leak

`freeblk()` recurses **while still holding its buffer**:

    buf = bread(dev, blk, 0);
    for (j = ...; j >= 0; --j)
        freeblk(dev, bn[j], level-1, b);   /* recurses, buf still held */
    brelse(buf);

Truncating a doubly indirect file therefore pins two buffers at once,
plus whatever `blk_free` needs for the chain block. Bounded at three,
so it will not exhaust twenty by itself - but the usable pool is
smaller than `NBUFS` suggests, and it will bring a small leak to the
panic sooner.

Also in `readi` (`inode.c` ~line 137): `bp` is not reset to NULL after
`brelse(bp)`, and the next iteration tests `else if (bp == NULL)`.
Worth a look - it reads as a use-after-release rather than a leak, but
it is the same neighbourhood.

## Filesystem state across these crashes

The inode accounting has held. Four hard crashes since the `blk_alloc`
fix and fsck has reported no systematic free-inode drift - the worst
was off by one, in the over-counting direction, which is an inode freed
in core whose disk copy did not get written before the panic. Ordinary
crash damage. Before that fix the same crashes left it 31 and then 148
adrift.

# A PSRAM arena as a general Fuzix facility

Briefing note, 2026-07-31. Companion to PC3-GFX-DESIGN.md; assumes
NOTES-process-memory.md and the swap notes in `config.h`.

## The proposition

Let a process ask the kernel for a region of PSRAM outside its own
image, and get back a base address and a length. The region does not
count against `PROGSIZE`, is not copied by `contextswitch`, is not
written to swap, and is not duplicated by `fork`. What the process puts
there — a garbage-collected heap, a symbol table, a framebuffer, a file
cache, a ring buffer — is its own business. The kernel's job ends at
"here is 1 MiB at 0x11100000, it is yours until you exit."

Calling it a heap is the likely first use, not the definition.

## Why this is available at all

Nothing new has to be invented. `pico_psram_region.template.ld`
declares the window `PSRAM(rwx) : ORIGIN = 0x11000000`, `psram_init()`
already brings it up at boot against the final `clk_sys`, and `main.c`
already proves it with a read/write test through the XIP window before
the kernel has finished booting.

More to the point, the port already treats PSRAM as address space in
two places:

* `devpsram.c` — `psram_disc_transfer()` is a plain `memcpy` to
  `PSRAM_BASE + (lba << 9)`. The "block device" is a memory window with
  a block interface bolted on top.
* `lineedit.c` — parks its state at
  `PSRAM_BASE + psram_size - PSRAM_RESERVE`, a 64 KiB region held back
  from the disc for exactly this reason.

So the facility proposed here is the third consumer of a mechanism that
exists and works. It is the first to expose it to userland, which is
where all the actual difficulty lives.

## What problem it solves

The 256 KiB ceiling is not an SRAM ceiling. `pagemap_realloc()` refuses
to grow past `PROGSIZE + UDATA_SIZE` and says why in its own comment:
*"growing past PROGSIZE would make swapout overwrite the neighbouring
slot."* The limit is the fixed-size swap slot, not the address space.
An out-of-image arena attacks the constraint at its actual source
instead of trying to raise a number that is load-bearing.

The second-order win is larger than the first. `contextswitch()`
`memcpy`s or `swap_blocks`es the whole image on every switch, and
`swapout()` writes all of it. Data that lives in an arena is never
moved, never cloned, never swapped. For a process with a large working
set, that is the difference between 256 KiB of block shuffling per
context switch and none. On a machine with a 320 KiB pool and a 256 KiB
ceiling — where more or less one big process is resident at a time —
that is the dominant cost, not a detail.

Plausible clients, roughly in order of how soon they could use it:

* `cc1`'s symbol tables. The compiler plan rejects monolithic compilers
  because *"a monolithic compiler must fit code, data, symbol tables and
  heap in one process"* — that is a slot-size argument, and this
  dissolves it.
* BBC BASIC's workspace, which currently probes `brk` until it gets
  `ENOMEM`.
* The graphics work: framebuffers and sprite data that are pure bulk.
* An editor's text buffer.
* MicroPython's GC heap, eventually.

## What it is not

Worth stating plainly, because the name invites all three assumptions:

* **Not protected.** There is no MMU and no MPU region in use. A wild
  pointer into the window corrupts another process's arena, or the swap
  device, silently. This is the honest cost of the design.
* **Not address space for text or data.** The window is executable, so
  running a binary from it would work. Don't — instruction fetch that
  misses the 16 KiB XIP cache is a QSPI transaction, and that cache is
  shared with flash XIP.
* **Not persistent.** `devpsram.c` already says so for the disc; the
  same applies here.

## The design

### Where the memory comes from

Generalise `PSRAM_RESERVE` from a constant into a three-way split fixed
at boot:

    [ 0             .. arena_base )      PSRAM disc / swap
    [ arena_base    .. top - 64K )       arena pool
    [ top - 64K     .. top )             kernel (lineedit)

Set `arena_base` from a boot parameter — `plt_param()` already parses
`kbd=` and `tty=`, so `psram=1M` costs almost nothing and makes the
split tunable without a reflash. That matters because the right size is
an empirical question and every megabyte is real money:

| arena | disc | swap slots (of 31) |
|-------|------|--------------------|
| 0     | 8128K| 31 |
| 512K  | 7616K| 29 |
| 1M    | 7104K| 27 |
| 2M    | 6080K| 23 |
| 4M    | 4032K| 15 |

`PTABSIZE` is 30, and the `config.h` comment records that the stock 15
ran dry a few processes past the old limit. 2 MiB puts the slot count
back below the process table; 1 MiB does not.

**The interaction to get right is `swapon`.** `_swapctl` takes the size
straight from userland and never consults the block device — it just
counts `SWAP_SIZE` chunks until the number runs out. So the
`swapon /dev/hdc 16256` in `rc` is the *only* thing deciding how much of
the PSRAM swap believes it owns, and if the arena moves under it, swap
will write through the arena without complaint.

Shrinking `blk->drive_lba_count` in `psram_disc_init()` is what makes a
stale `rc` fail safely: `blkdev.c:80` bounds-checks `blk_op.lba` against
`drive_lba_count`, so an overrunning swap write becomes a loud `EIO`
rather than silent corruption. Confirm the swap path actually goes
through that check rather than around it before relying on this — it is
the single assumption the whole safety story rests on.

### The allocator

Deliberately trivial: a fixed table of 4–8 entries, each
`{ owner, base, len }`, first fit, merge adjacent free entries on
release. This is a slot table like `allocation_map`, not a `malloc`.
Regions should be 4 KiB-aligned and 4 KiB-granular — partly for
`BLOCKSIZE` consistency, partly because NOTES-process-memory.md is a
101-line record of what a four-byte alignment mistake costs on this
machine.

Zero on allocation. PSRAM survives a warm reset, `psram_disc_init()`
clears only the first 2048 bytes, and handing a process the previous
run's contents is exactly the kind of leak the boot-udata bug already
demonstrated. Measure the cost first: a 1 MiB `memset` through the QMI
is not free, and if it is bad enough the answer is a zero-on-first-use
flag rather than skipping it.

### The interface

An ioctl on `/dev/sys` (4:6), following `PICOIOC`/`GFXIOC`/`SNDIOC`. Not
a new syscall: a syscall number is a change to a table shared by fifty
platforms, for a facility exactly one of them has.

    PSRAMIOC_ALLOC  0x000A   struct psram_req { uint32_t len, base; }
    PSRAMIOC_FREE   0x000B   data -> base
    PSRAMIOC_STAT   0x000C   struct psram_stat { total, free, largest; }

`ALLOC` rounds `len` up, fills in `base`, or returns `ENOMEM`. The
address goes to userland raw, because with no MMU there is nothing else
it could be — the arena has one address and a process can only use it by
knowing it. State that in the header rather than hiding it.

### Lifetime

* **exit** — `process.c:942` already calls `pagemap_free(udata.u_ptab)`
  in `doexit`. Release arenas on the next line. Anything else leaks a
  megabyte permanently; there is no OOM killer here to recover it.
* **exec** — release, on the same footing as `brk`.
* **fork** — the hard one, and it needs deciding before any code is
  written. `clonecurrentprocess()` copies the image; there is no second
  megabyte to copy the arena into, and no MMU means no copy-on-write.
  So either the child shares it (two processes writing one region with
  no protection between them) or the child loses it. **Recommend: the
  arena stays with the parent, the child owns nothing.** Note the
  wrinkle that makes this sharp — the child's *image* still contains the
  pointer, and will cheerfully dereference a region it no longer owns.
  There is no way to fault on that. It has to be a documented contract.
* **swapout / swapin** — nothing to do, which is the entire point. But
  `p_size` and `free(1)` will now under-report, so accounting needs a
  separate PSRAM row or the tools start lying.

### Cache and coherence

`config.h` already documents the hazard: PSRAM is cached write-back
(`XIP_CTRL_WRITABLE_M1`), dirty lines live in the 16 KiB XIP cache, and
invalidating without cleaning throws them away — which is why
`rawflash.c` walks `XIP_MAINTENANCE_BASE` before flash operations,
following MMBasic's `FileIO.c`.

Adding a userland writer changes the consequence rather than the
mechanism. Today a missed clean corrupts swap. Tomorrow it silently
eats a process's data, which is much harder to attribute. Two things
follow: every existing invalidate path needs re-auditing with that in
mind, and if DMA or core1 is ever pointed at an arena, the nocache
window at `0x1c000000` needs an explicit decision instead of an
accident.

## Performance shape

Sequential access through the QMI is respectable. Random access is one
QSPI transaction per cache miss, against a 16 KiB cache shared with
flash XIP. Anything that walks the whole arena — a GC mark phase, a
symbol table rehash — is bounded by misses, not by its own algorithm.

The design rule that falls out: **arena for capacity, SRAM for the hot
working set.** Any client big enough to want an arena should keep its
frequently-touched structures in the image and put bulk in PSRAM.
Whoever goes first should measure a full linear walk and a full random
walk before anyone commits to a size.

## Staging

1. **Split the region.** Boot parameter, shrink the disc, report the
   three-way split at boot. No API, no clients. This alone proves the
   `swapon` interaction is safe and is independently useful.
2. **Table and ioctls,** plus release-on-exit. One test program:
   allocate, fill with a pattern, verify, fork, exec, exit, confirm the
   space comes back. `free` and `PSRAMIOC_STAT` must agree.
3. **First client — something already in the tree,** not MicroPython.
   `cc1`'s symbol table is ideal: it exists, it is size-constrained
   today, and it can be run both ways for comparison.
4. **Accounting and documentation.** A PSRAM row in `free`, a section in
   FUZIX-PC3-MANUAL.md, the fork contract written down where someone
   will actually read it.
5. **Then** the interesting clients.

## Open questions — answered in implementation, 2026-07-31

1. **Fork semantics.** Decided as recommended: the arena stays with
   the parent, the child owns nothing.  arenatest proves the child's
   cross-free is refused and the parent's data survives the fork.
2. **Does the swap write path honour the `blkdev` bounds check?**
   NO — not as it stood.  `translate_lba()` bounds-checked only the
   FIRST sector; the transfer loop advances `blk_op.lba` without
   re-checking, so a multi-sector write straddling the end ran off
   the device silently.  Fixed: the whole transfer is bounded, for
   partitions too.  The "verify, don't assume" instinct was right.
3. **How much?** Shipped at 1 MiB (`PSRAM_ARENA_DEFAULT`), `psram=`
   boot parameter to retune.  rc's swapon updated to 14208.
4. **MPU.** Still rejected on purpose, unchanged.
5. **Zeroing cost.** Imperceptible at 1 MiB against the rest of an
   allocation's lifetime; kept unconditionally.

**The question the brief missed — syscall address validation.** The
platform's `valaddr()` accepts only the process image, so the first
client died instantly: any `read()`/`write()` with an arena buffer is
EFAULT.  An arena the process owns is now equally legitimate
(`arena_valaddr()` consulted from `valaddr()`), and every future
client gets file I/O into its arena for free.  Any port copying this
design must copy that decision too.

## Status

Stages 1–3 are implemented and verified on hardware (2026-07-31):
the split with the `psram=` parameter, the allocator and ioctls with
release-on-exit/exec (arenatest exercises alloc/zero/pattern/fork/
free/stat/leak-reclaim), and the first client — cc2's tables, whose
static form had in fact outgrown the 256K process entirely.  The
PC3 compiles C on itself again, with near-BIG_TABLES limits.
Remaining from the staging list: the accounting row in `free` and
the manual section.

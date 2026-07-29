# Inode double free — briefing note

2026-07-29. **SOLVED.** See section 0 for the answer; the rest is the
hunt, kept because most of what it eliminates is still worth knowing.

## 0. The answer

**It was never an inode bug at all. It was a two-byte buffer overrun in
the *block* allocator, landing on the inode free list count.**

`blk_alloc()` refills the block free list from a chain block:

    blktok(&dev->s_nfree, buf, 0,
        sizeof(int) + FILESYS_TABSIZE * sizeof(blkno_t));

`s_nfree` is a `uint16_t`. On the 8 and 16 bit machines Fuzix grew up
on `sizeof(int)` is 2 and the two agree - the comment in `blk_free()`
even says so: *"nfree must directly preceed the blocks and without
padding. That's the assumption UZI always had."* On this 32 bit target
`sizeof(int)` is 4, so the copy runs **two bytes past the end of
`s_free[]`**. And in `struct filesys`, what sits immediately after
`s_free[]` is:

    blkno_t       s_free[FILESYS_TABSIZE];
    int16_t       s_ninode;              <-- the inode free list count
    uint16_t      s_inode[FILESYS_TABSIZE];

`blk_free()` wrote the same over-long field, so those two spare bytes
on disk hold whatever `s_ninode` happened to be when that chain block
was written. Every block free list refill therefore **restored a stale
inode free list count**, and every already-popped slot in `s_inode[]`
above the true count came back to life - naming inodes that were by
then live files.

That is why:

* no `i_free()` call ever appeared in the trace, and no scan ran - the
  list grew without either;
* the same small set of inodes recurred: they are the most recently
  popped slots, which are exactly the files just created;
* it only bit under heavy *block* allocation, which is what triggers a
  refill, which is why big compiles provoked it and small ones did not;
* fsck always found the structure sound: the damage was in a counter
  and a cache, not in the filesystem.

Offsets 0..101 of the chain block are unchanged, so existing cards,
images and host tools still interoperate; the fix simply stops touching
the two bytes past the block list.

**This is a 32 bit portability bug in shared Fuzix code** and will be
present on any 32 bit target. Worth sending upstream.

### Verified

70 consecutive on-target compiles with no kernel message of any kind,
where the guards previously fired within three. `df -i` before and
after 28 compiles: 1349 free inodes both times, exactly conserved.
fsck afterwards reports no count discrepancy and all five passes clean.
All 14 compiler samples still build on the board and match gcc.

---

## The hunt

What follows was written while it was still open. Kernel commits
`27a82379f` and `9006603d5` plus the V7 comparison in section 4b.

---

## 1. The symptom

Running the on-target C compiler, a pass fails to create a file:

    $ cc strs.c
    i_open: bad inode 649 new mode 81ED nlink 1
    strs.bc: File table overflow

After two or three compiles the machine cannot create files at all. It
was first seen the day the compiler started running on the board,
because nothing before it had ever created and deleted several files
per second.

**`File table overflow` is a lie.** `filesys.c` maps *every* `i_open`
failure onto `ENFILE` in the create path, so that is the message
whatever went wrong, and it sends you to `OFTSIZE`. The table is not
full. The real error is the `i_open:` line above it.

### Reproducing it

    cd /root/cc
    for f in sieve strs rpn libtest ll2 dbl fp width3 sw2; do
        rm -f $f.bc; cc $f.c
    done

With the guards in place this now completes, but logs each time the
fault is contained. A silent run means it is fixed; the messages below
mean it is not.

    i_alloc: 664 in free list but in use, skipped
    i_free: 664 freed twice

---

## 2. What it actually is

**An inode may appear on the free list at most once. It appears twice.**

`i_alloc` therefore hands out an inode that is a live file. `i_open`
sanity-checks the inode it is given, sees a mode and a link count on
something that is supposed to be newly allocated, and fails. That check
firing is what saved the filesystem: had it not, two files would have
shared an inode, which is real corruption.

Traced over one compile, with every allocation and free printed
(`A<ino>@<depth>`, `F<ino>@<depth>`, `S<n>` for a scan refill):

    S50 A657@49 A656@48 F656@48 A656@48 A655@47 A648@46
        ........ A657  ->  bad inode 657 mode 81A4 nlink 1

657 is popped at depth 49 and again later with no free in between.
With a duplicate check added to `i_free`:

    i_free: 646 already free at 48 of 49
    i_alloc: 646 in free list but in use, skipped

`i_free` has **exactly one caller** — the freeing branch of `i_deref`
(`if(!(ino->c_refs) && (ino->c_flags & CDIRTY))`, when `i_nlink` is 0).
So `i_deref` reaches that branch twice for the same inode.

---

## 3. Eliminated — do not re-investigate

* **PSRAM and the overclock.** The obvious suspect: the machine is
  overclocked and the swap disc is in PSRAM. Swap was moved to the SD
  partition (`swapon /dev/hdb3 8192`) and the fault reproduced exactly.
  Swap is back on PSRAM.
* **Swap sizing.** Checked at the same time and sound. `rc` registers
  16256 blocks, the loop in `A_SC_ADD` stops at 31 slots of `SWAP_SIZE`
  512, and the disc is 16256 blocks after `PSRAM_RESERVE`. `MAX_SWAPS`
  is 32, which only oversizes the table. Nothing overruns.
* **The compiler.** `cc optest.c` runs to completion, plain file
  creation works throughout, and every sample compiles correctly on the
  host.
* **A full disk or a full table.** `df -i` showed 1380 inodes free,
  `ps` showed nothing leaking, 58 MB free.
* **A dirty filesystem.** `fsck` reports clean, correctly — the on-disk
  filesystem really is consistent, because the free list is only a
  cache.
* **A damaged card.** A freshly built image reproduced it after two
  compiles.
* **Rebooting.** Does not clear it.
* **`staticfast` re-entrancy in `i_alloc`.** Worth a look given this
  port added preemption, but `cpu-armm0/cpu.h` defines `staticfast` as
  `auto`, so those are ordinary locals.
* **The disk scan in `i_alloc`.** Instrumented to check its own output
  for duplicates. It never produced one, which is what pointed at
  `i_free`.

---

## 4. What was changed

### A real fix

**`fmount()` discards the on-disk free list** (`fp->s_ninode = 0`) and
lets `i_alloc` rebuild it by scanning.

The list is a cache of what was free when it was written, and two
things routinely invalidate it behind the kernel's back: `ucp` builds
the card image on the host, and `rc` runs `fsck -a -y /` against a root
the kernel has *already mounted*. fsck knows the list cannot be trusted
and sets `s_ninode` to 0 on disk for exactly this reason — but we read
the superblock before it ran, kept using the stale copy, and wrote it
back over fsck's correction on the next sync. That is why fsck said
clean and why rebooting changed nothing.

### Two guards — containment, not cure

**`i_free` refuses an inode already on the list.** Freeing something
already free is a no-op, not a reason to list it twice.

**`i_alloc` reads each inode it takes off the list** and skips it if it
has a mode or a link count, instead of handing out a live file. The
block is nearly always in the buffer cache.

**`i_open` re-reads a freshly allocated inode** when it found a cached
table entry for that number. The lookup loop takes a matching entry
whatever its reference count and that path does not read the inode, so
a new allocation could be validated against whatever the previous file
using that number left behind. This narrowed the fault but did not
remove it.

### A better message

`i_open` now names the inode, says whether it was newly allocated or
looked up, and gives its mode and link count. The old
`i_open: bad disk inode` distinguished nothing.

---

## 4a. It has got worse, and it is now the blocking task

**2026-07-29, later the same day.** Running the compiler test suite on
the board — twelve samples, each one creating and unlinking five
temporaries — the machine died: video gone, no console, unrecoverable
by reset, needed BOOTSEL and a reflash. On the next boot fsck said

    Filesystem was not cleanly unmounted.
    Free inode count in superblock is 1360, should be 1391. Fix? y
    Pass 2: Rebuilding free list...

and **a 24K file written four minutes earlier had vanished**
(`/root/cc/bcrun`). The guards had been firing throughout, including
during plain `uud` transfers with no compiler involved:

    i_alloc: 659 in free list but in use, skipped
    i_alloc: 673 in free list but in use, skipped
    i_alloc: 647 in free list but in use, skipped

So the guards contain the *symptom* — an allocation of a live inode —
but the underlying corruption is still accumulating, and under enough
create/unlink traffic it now takes the machine down and loses data.
This is no longer a nuisance to work around.

It is also what stands between this port and the next thing worth
doing: running the c-testsuite `tests/single-exec` conformance set
(https://github.com/c-testsuite/c-testsuite) on the board. That is
hundreds of compile-and-run cycles back to back, which is precisely the
workload that triggers this, and there is no point starting it until
the filesystem is trustworthy.

## 4b. The V7 comparison, and what it fixed

Comparing `i_deref()` against V7's `iput()` (and `i_alloc()` against
V7's `ialloc()`) found five genuine divergences. **None of them was the
root cause**, but all five are real and all five are fixed, because
each was a latent fault of its own:

| | V7 | Fuzix, before |
|---|---|---|
| ref drop | after the destruction | before `f_trunc` |
| free condition | `i_nlink <= 0` alone | also required `CDIRTY` |
| after freeing | `i_flag = 0; i_number = 0` | entry still findable by number |
| lock across the rebuild | `s_ilock` + sleep/wakeup | none |
| rebuild scan | skips inodes that are in core | did not |

What each was actually costing:

* **Dirty-gated free.** An inode whose last link and last reference
  went away without anything dirtying it was never returned to the
  free list *and* never had its mode cleared on disk. A silent leak.
* **Entry not invalidated.** A dead cache entry could still be matched
  by number by a later `i_open`, including for a newly allocated inode
  that reused the number, and validated against a copy of an inode that
  no longer described anything.
* **Ref dropped early.** From `f_trunc` (which does block I/O) to the
  end of the function the entry read as unreferenced, so `i_open` would
  hand it out as a free slot.
* **No in-core check in the scan.** `_pipe()` sets `i_mode` to F_PIPE
  in core and never writes it, so the disk still says that inode is
  free. A scan would list a live pipe.
* **No lock.** `i_alloc` refills `s_inode[]` from index 0 and assigns
  `s_ninode` only at the end. The function's own comment asked for the
  lock. Measurement afterwards showed no actual concurrency on this
  workload, so it was not the cause here - but it is still required.

Also fixed alongside: `i_open` leaked a freshly allocated inode on its
ENFILE and read-failure paths, which is where the *other* half of the
"free inode count should be N+31" drift came from.

**Do not undo these while cleaning up.** They are correct against V7
and they are each a bug in their own right.

## 5. Outstanding

**Nothing on the inode double free.** It is fixed and verified; see
section 0.

Two things remain worth doing:

**`rc` runs `fsck -a -y /` on an already-mounted root.** fsck writing a
filesystem the kernel has cached is unsound in general; the `fmount`
change only covers the one field it touches. The clean fix is to fsck
before mounting read-write, or to have fsck refuse a mounted
filesystem.

**The `ENFILE` mapping** in the create path should distinguish "no
inode table slot" from "i_open rejected the inode". It cost an hour
here and will cost it again.

### Historical: what the double deref looked like

Known:

* one call site, so it is `i_deref` — no need to hunt callers of
  `i_free`;
* the scan is innocent;
* **nothing holds a reference at the time of the second free** — a
  probe walking `i_tab` for an entry with `c_refs != 0` never fired;
* the inodes involved are always the compiler driver's four temporaries
  (`.pp`, `.tok`, `.ir`, `.symtmp`), created and unlinked every run —
  the create-then-unlink pattern is the trigger, and `cpp` adds a fifth
  with `.cppswap`, which it opens and unlinks immediately;
* the in-core inode disagrees with the disk: an inode is freed whose
  in-core copy says `nlink 0` and cleared mode, while the on-disk copy
  is still live;
* it is upstream Fuzix code, not PC3-specific.

The next probe to write: log `c_num`, `c_refs`, `c_flags`, `i_mode` and
`i_nlink` at **entry** to `i_deref` rather than at the free branch, and
correlate the two frees of one inode. The question to answer is what
put the entry back into a state where `CDIRTY` is set and `i_nlink` is
0 for a second time.

Watch `unlinki()` and the `_unlink` path, and the interaction between
`f_trunc` and the `CDIRTY` test — `wr_inode` clears `CDIRTY` only on a
successful write.

### Also outstanding, related

**`rc` runs `fsck -a -y /` on an already-mounted root.** fsck writing a
filesystem the kernel has cached is unsound in general; the `fmount`
change only covers the one field it touches. The clean fix is to fsck
before mounting read-write, or to have fsck refuse a mounted
filesystem.

**The `ENFILE` mapping** in the create path should distinguish "no
inode table slot" from "i_open rejected the inode". It cost an hour
here and will cost it again.

---

## 6. How to instrument it again

All of this was done by editing `Kernel/filesys.c`, rebuilding and
reflashing without touching the board:

    cd Kernel/platform/platform-rpipico/build && make -j4
    # then, from devtools/
    python fzsh.py 25 "sync; remount -n / ro; sync" "picoctl flash"
    # board appears as drive F:, copy build/fuzix.uf2 onto it
    python fzsh.py 25 "hdb2" "root" "cd /root/cc; rm -f fp.bc; cc fp.c"

`kprintf` works from anywhere in the filesystem code and goes straight
to the console. Keep the prints short — `A%u@%u` rather than a
sentence — or the console becomes the bottleneck and the timing
changes.

**Always `remount -n / ro` before `picoctl flash`**, or the card needs
an fsck on the next boot, which perturbs exactly the state being
investigated.

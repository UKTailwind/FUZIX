# Inode double free — briefing note

2026-07-29. **Open.** Contained, not cured.

The filesystem is fundamental, so this is written to be picked up cold.
It records what the fault is, what was eliminated, what was changed and
what is still wrong. Kernel commits `27a82379f` and `9006603d5`.

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

## 5. What is outstanding

**The double deref itself.** `i_deref` reaches its freeing branch twice
for one inode and nothing above knows why.

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

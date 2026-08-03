# Three allocator bugs, 2026-08-03

All three surfaced the same afternoon, chasing why a translated BASIC
program could no longer save an image. See NOTES-process-memory.md for
the layout these live in.

## `pagemap_alloc` kept what it took

It claims blocks one at a time and returned ENOMEM part way through
still holding all of them. Its caller, `newproc()` in the generic
kernel, abandons the slot on ENOMEM *without* calling `pagemap_free` --
and the slot stays `P_EMPTY`, so nothing else ever frees it either.

Measured: fresh boot 68 KB used; one failed fork; 200 KB used, and it
stayed there for the rest of the boot. 132 KB of a 312 KB machine, per
failed fork. `pagemap_realloc`'s grow loop had the same shape and now
unwinds too -- there it is worse than a leak, because `p_top` still says
`oldblocks`, so a later grow would hand out a second map entry with the
same block index.

## fork needed the process resident twice

`clonecurrentprocess` copies parent blocks to child blocks with both in
RAM, and `swapvictim` can never evict the current process, so nothing
bigger than half of USERMEM could fork at all. bcrun with a program
loaded is ~172 KB of 312 KB, so every `SAVE IMAGE` -- which forks
`/usr/bin/saveimage` -- died with "cannot start a program". It had been
marginal for a long time; two TOTALMEM trims and 4 KB of bcrun growth
pushed it over, which is why it looked like a regression in bcrun.

The fix uses what the new swap already provides: the two images are
identical at the instant of the fork, so which one is called the copy is
free. `pagemap_alloc` stages the **parent** into a PSRAM arena and the
child keeps the resident blocks it is already executing in; nothing
moves and nothing is freed, only the map labels and `swapaddr[]` change.
The parent's udata rides along in block 0 and `dofork` saved its SP into
it beforehand, so the image is a complete swapped-out process and the
ordinary swapin path brings it back. Costs one PSRAM round trip on a
command that runs once.

`fork_stage` carries the region from `pagemap_alloc` to
`clonecurrentprocess`. Interrupts are off from `newproc()` through
`dofork()`, so one word is enough.

## PROGSIZE was still the old slot size

262144 was how big a fixed swap slot was, and swap has not been slots
since the arena landed. It stopped bcrun loading a 140 KB translated
program on a machine with 8 MiB of PSRAM -- "out of memory (bind
table)". Now `USERMEM - UDATA_SIZE`: a process may fill user memory,
because everything else can swap and fork no longer needs a second copy
in RAM.

## Verified on the PC2 (COM14)

* `imgloop.bc` -- five rounds of SAVE IMAGE / LOAD IMAGE, each forking
  an external binary out of a 172 KB bcrun. Was impossible before.
* memory returns to its boot figure afterwards: 48 used + 20 swapped =
  the 68 it booted with.
* the eclipse, translated and compiled **on the board**, 3.307 s.

## Reflashing without touching the board

`sync; remount -n / ro; sync` then `picoctl flash`; the board appears as
a USB drive; copy `fuzix.uf2`. Going into BOOTSEL with the card mounted
read-write costs an fsck cycle on the next boot, and if the crash
happened mid-transfer the uuencoded text ends up typed at the `bootdev:`
prompt.

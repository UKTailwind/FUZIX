# "panic: no free buffers" — found

2026-07-29. **FIXED.** Not a leak. A 16-bit wrap in `freebuf()`'s LRU
selection that only misbehaves where `int` is 32 bits.

The inode double free that preceded it is a separate, earlier fault
(`NOTES-inode-freelist.md`).

## The bug

`Kernel/devio.c`, `freebuf()`, as it was:

    regptr bufptr oldest;
    register int16_t oldtime;

    oldest = NULL;
    oldtime = 0;
    for (bp = bufpool; bp < bufpool_end; ++bp) {
        if (bufclock - bp->bf_time >= oldtime && !bisbusy(bp)) {
            oldest = bp;
            oldtime = bufclock - bp->bf_time;
        }
    }
    if (!oldest)
        panic(PANIC_NOFREEB);

`bufclock` and `bf_time` are both `uint16_t`.

**Where `int` is 16 bits** - every 8-bit Fuzix target - they promote to
`unsigned int`, the subtraction wraps modulo 65536, and the age is
correct across a wrap. The code is right on z80.

**Where `int` is 32 bits** - every ARM target, including this one - they
promote to *signed* `int` instead. No wrap happens. A buffer stamped
before `bufclock` last wrapped gives a large negative age: at
`bufclock = 3`, a buffer stamped at 65500 ages as `3 - 65500 = -65497`.

`oldtime` starts at 0, so the test is `age >= 0` on the first iteration.
Every negative age fails it. Immediately after `bufclock` wraps, *every*
buffer has a negative age, `oldest` stays NULL, and the kernel panics
with the entire pool free.

That is the "leak" that consumed several sessions. There was never a
leak. Every `bread`/`brelse` audit came back balanced because it was
balanced.

## How it presented, and why each clue misled

* **"It dies while idle."** True, and it was read as proof of a leak -
  an idle machine cannot be under buffer pressure. It is really proof
  of the opposite: the pool was free and the panic fired anyway.
* **"Larger C files precede the crash."** The user's observation, and
  the key that cracked it. `bufclock` advances once per buffer
  acquisition, so time-to-panic is measured in buffer traffic, not
  wall clock. Bigger files reach the wrap sooner. Nothing about
  indirect blocks, which is where that clue first pointed.
* **The screen going blank first.** A consequence of the panic.

## The proof

The pool dump wired into `freebuf()` before the panic printed this:

    buf 0: dev 18 blk 8031 busy 0 dirty 0
    ... 18 of the 20 with busy 0 ...
    panic: no free buffers

Eighteen free buffers and it could not find one. That is not ambiguous.

`bufclock` at the last successful sample was 64775, and the panic came
during the next step. Predicted before it happened, from the arithmetic.

## The fix

Truncate the age back to `uint16_t` and compare unsigned, which is the
ordinary LRU-with-wrap idiom and correct at either int width:

    register uint16_t oldtime;
    uint16_t age;
    ...
        age = bufclock - bp->bf_time;
        if (age >= oldtime && !bisbusy(bp)) {

`/tmp/wraptest.c` (host, throwaway) ran both versions over the cases:

    all free, before wrap        clock 65535 -> old   0   new   0
    all free, just wrapped       clock     3 -> old  -1   new   0
    all free, well past wrap     clock   200 -> old  -1   new   0
    normal, oldest is 0          clock  2000 -> old   0   new   0
    straddling wrap, oldest 0    clock    20 -> old   6   new   0
    all busy (must be -1)        clock  2000 -> old  -1   new  -1

Confirmed on hardware. The same `bufwatch.py` workload that killed the
old kernel at cycle 6 was rerun against the fixed one and walked
straight through the wrap:

     9 cleanup       0     0     17   62874
    10 send          0     0     17   65045
    10 decode        0     0     17     651     <- bufclock wrapped here
    10 compile       0     0     17    3556
    11 send          0     0     17    6326

`busy 0` throughout, no panic, and it kept going.

`-1` is `oldest == NULL`, ie the panic. Note line 5: straddling a wrap
the old code also evicted the *wrong* buffer, so this was quietly
degrading the cache the whole time, not only crashing at the boundary.
Line 6 matters too - "every buffer genuinely busy" must still return
NULL, so the panic keeps its meaning.

## Upstream

`freebuf()` is unmodified Fuzix; `git log` on the LRU loop shows only
the debug dump added here. So this is an upstream portability bug that
bites the 32-bit ports (ARM, riscv32, esp32) and not the 8-bit ones.
Worth sending upstream.

## Instrumentation left behind

Worth keeping - it is what turned a guess into a measurement.

* **`utils/bufs.c`** - `bufs`, `bufs -v`, `bufs -q`. Reports the pool
  over the `PIOC_BUFSTAT` ioctl on `/dev/proc` (`Kernel/devsys.c`,
  collector `bufstat_report()` in `devio.c`, shared layout in
  `Kernel/include/bufstat.h`).
* **`bf_pid` / `bf_call`** in `struct blkbuf`, stamped by `bufown()` in
  `block()`. Four bytes a buffer; names the pid and syscall that pinned
  each one. It said the two busy buffers at the panic were legitimately
  held, which is what ruled the leak out.
* **`devtools/bufwatch.py`** - runs send/decode/compile/run in a loop
  and samples the pool between every step. `--pad N` grows the source,
  because every sample in the tree is under 4K and the wrap is reached
  by volume. This is what reproduced the panic on demand at cycle 6 of
  a 40K workload.
* The pool dump in `freebuf()` before `panic()`.

## Related, and now also fixed — `timer_expired()`

Found while sweeping for the same bug class, fixed separately.
`Kernel/timer.c` had:

    timer_t set_timer_duration(uint16_t d) { ... a = d; a += ticks.h.low; return a; }
    uint8_t timer_expired(timer_t t)       { return t < ticks.h.low; }

A plain magnitude compare where the counter wraps. Once
`ticks.h.low + duration` exceeds 65535 the deadline is a *small*
number, below the current tick, and the timer read as expired the
instant it was created; the mirror case is a wrapped deadline that
fails to expire for most of a turn of the counter, nearly two hours at
ten ticks a second. Live here through `devsd.c`'s SD command timeouts,
where it presents as an occasional spurious I/O error.

Now computes the distance past the deadline modulo 65536, which is what
a wrapping counter needs;  `set_timer_duration()` already guarantees the
deadline is inside half a turn by rejecting a duration of 32K or more.

**Note the difference from the panic above:** this one is wrong at
*either* int width. It is not the 16-vs-32-bit promotion trap, just a
comparison that always needed to be modular. Two adjacent bugs with
similar symptoms and different causes - worth keeping straight.

## Filesystem state

The inode accounting held throughout. Five hard crashes since the
`blk_alloc` fix with no systematic free-inode drift.

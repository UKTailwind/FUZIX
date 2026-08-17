# PLAN-codesize: fitting native programs in the process pool

2026-08-17.  Written after PicoMan became the first game that had to be
built `BCODE_ONLY=1` to load.  The user's verdict: PicoMan is not a
large program by MMBasic standards, and dropping to bytecode is a
retrograde step.  This reviews the whole toolchain and ranks every
option for reducing program size without compromising performance.

## STATUS 2026-08-17 (later the same day): Option 1 executed

Two density changes landed in FUZIX/Applications/CC (aead0d5ab,
a35ba6e81), gates green after each (fcctests 54/54, qemutests 55/55;
cc2-only - the board's bcrun is untouched and needs no update):

1. compare;BOOL;JFALSE chain fusion in t_boolbranch - cc1 normalises
   truthiness with a BOOL after nearly every comparison, and the old
   fusion only looked one op ahead, so every condition paid an 8-byte
   flagval plus a re-test.
2. Uniform 8-byte call sites + per-callee thunks - the old scheme left
   every runtime call on a 14-byte trampoline (432 sites in PicoMan vs
   261 direct); now every site is subs/bl/adds and gen_end points the
   BL at the native callee or a shared 14-byte thunk.  No ABI change;
   thumb_link_calls' pattern-match rewrite deleted with the pattern.

Measured: PicoMan translated fns 73,716 -> 64,486 native (1.98x ->
1.73x, -12.5%); board-shape code 137,713 -> 129,281.  Vaders 50,406 ->
48,488 - and vaders' main (1,856 bc) translates under the caps, so
vaders is already FULLY native at a 51K image.  PicoMan's monolithic
63.5K main-line is the outlier.

**Option 2 verdict, re-run with the improved emitter**: main is 63,565
bc -> 114,674 native (1.80x); all-native PicoMan is code 244,158 +
data/bss 49,430 = 293,588 image, ~418K of process against the ~310-320K
ceiling.  Still ~100K over - no plausible further densification closes
it.  Even the hybrid (main native, subs bytecode) lands at ~326K, just
over the line.  **Taking main native waits on Option 3 (code[] in a
PSRAM arena) or equivalent structural relief; emitter work alone cannot
get there.**  Remaining Option-1 tail (eqop-double helper slots, switch
tables, guard sharing, const64 short forms) is worth ~2-3K on this
corpus - do it opportunistically, not as the plan.

Board verification still owed: rebuild the board cc2 from this source,
reinstall (hand-install list in pc3-v016-unreleased), rebuild the
games, and run the cc-perf A/B protocol - the changes remove executed
instructions on hot paths, but the board is the perf authority.

## STATUS 2026-08-17 (evening): the PicoMan review changed the picture

**Option 3 (code in PSRAM) is REJECTED** - user decision, and the
reasoning is sound: arrays, strings (bcrun's VM heap), and framebuffers
F and L already live in PSRAM behind the one 16K XIP cache shared with
kernel flash; adding instruction fetch would thrash what the data
already contends for.

**The program review found the real story.  PicoMan's footprint was
dominated by ONE LINE**: `On Error Skip` guarding the first `Blit
Close`.  One ON ERROR anywhere makes mmbc emit checked arithmetic for
the whole program (the checks-gating rule working exactly as designed):

    with On Error:    101K bytecode, main 63.5K (uncompilable), mixed 129K
    without:         38.4K bytecode, main 14.4K -> 26.1K NATIVE,
                     fully native image 132K, process ~258K - FITS

samples/picoman.bas now tracks its buffers in BlitOpen() instead
(test-and-branch for trap-and-skip, behaviour identical), builds fully
native under the DEFAULT board caps - all 55 functions, no bails - and
the BCODE_ONLY note is gone from mkexamples.sh.  Board re-test owed.

The manual gained the chapter: FUZIX-PC3-MANUAL.md "Making a big
program fit - and run fast" (#making-it-fit), five rules with these
measurements: ON ERROR ~2.6x code; thin main line (one function, the
caps, 2.7x if it stays bytecode); locals 2-3B vs globals 5B/reference
(36% of PicoMan's old main was global addressing); fold repetition
(a 7-arg Circle is 120-140B per appearance); DATA is nearly free.
Plus the instruments (THUMB_VERBOSE=1 cc, ls -l, BCODE_ONLY).

## Revised queue

1. **Hybrid native/bytecode selection in cc** (user-directed next):
   the system decides what translates when the whole program cannot.
   Design notes: (a) estimate the load footprint (floor + code + mem
   vs the pagemap ceiling) in the cc driver and derive THUMB_BUDGET
   instead of the all-or-nothing BCODE_ONLY; (b) budget is first-come
   and mmb2c emits main LAST, so either mmb2c emits main() first
   (prototypes make order free; h_entry follows the symbol) or cc2
   learns a priority (main, then smallest-first maximises translated
   count); (c) report what stayed bytecode, THUMB_VERBOSE-style, so
   the user knows.  With the ON ERROR lesson banked this is a safety
   net rather than the main path - PicoMan no longer needs it.
2. **Kernel graphics library** (user-floated, promising): move the
   mmb_*.h drawing statics (circle/triangle/linew/tedge/rbox/ring/
   blit...) into the kernel and export them PICOIOC_LIBM-style from
   flash - append-only table, header shims keep the mmg_* names so
   programs and the C manual's API are unchanged.  Worth ~15K bc /
   ~28K native per graphics program (21% of fixed PicoMan's image),
   for every program at once.  Perf: gcc -O2 (roughly offsets XIP
   fetch cost); the primitives end in pixel-batch ioctls anyway; the
   QMI-contention concern applies to flash as to PSRAM but the kernel
   already fetches from flash and these loops fit the cache.  Gate on
   a ripple/blitbench A/B.  Note the libm precedent netted +21% WHEN
   RAM was the constraint.
3. Density-pass tail (eqop-double slots, switch tables, const64
   short forms) - opportunistic, ~2-3K.

Not doing: code/data in PSRAM (rejected above); bcrun-text-to-flash
(superseded by 1+2 unless a program outgrows even those).

Everything below is measured on today's toolchain (host build of
samples/picoman.bas, THUMB_VERBOSE=1) or quoted from the tree with a
file reference.  Nothing is estimated where a measurement exists.

## 1. Anatomy of the problem (PicoMan, 1,244 BASIC lines)

| quantity | bytes | note |
|---|---|---|
| bytecode, whole program | 100,773 | `BCODE_ONLY=1`, version-1 object |
| ... of which `main` | 63,565 | the whole BASIC main-line, one C function |
| ... 54 other functions | 37,208 | f_* subs/functions + mmb_*.h statics |
| native Thumb-2 for those 54 | 73,716 | **1.98x expansion**, uniform 1.6-2.6x |
| board-shape code segment | ~137.5K | native 54 + bytecode main (reclaim on) |
| data + bss | 49,430 | DATA already pruned to live columns |
| bcrun mem[] request | ~60K | data+bss+mmrt+8K stack room+slack |
| sym+strtab+bind, resident | ~7K | kept for the whole run |
| bcrun's own floor | **~104.5K** | text 67.8K + ELF reloc 13.7K + data/bss 13.5K + udata 1.5K + stack 8K (CC/REVIEW-2026-08-06.md:137-154) |
| **total, native attempt** | **~310-315K** | |

The ceiling is not 336K.  `pagemap_realloc` (platform-rpipico/
swapper.c:328-404) caps a growing process at
`(84 - largest_neighbour()) * 4096` so the biggest other process can
still swap in.  With init + a shell resident that is ~310-320K.
PicoMan's native build sits exactly on the line; whether it loads
depends on what else is resident.  That is the observed "won't load".

Two structural facts follow:

1. `main` is not translated: 63,565 bytes of bytecode exceeds the
   board's 48K translation buffer (`TMAX`, backend-thumb.c:56-77) and
   the 40K size policy (`THUMB_MAXFN`).  **Every game's main loop runs
   interpreted at the known 2.7x penalty.**  The cliff is also the only
   reason the game loads at all: main at 1.9x would be ~120K of Thumb.
2. All-native PicoMan would be ~195K of code, ~367K of process.
   **No emitter improvement alone reaches that** - it needs -45% native
   size, and the backend is already fairly dense (1.98x from a compact
   bytecode, with push/op fusion, constant folding, direct BL linking).

So the review is really two problems:
- **A. Fit today's games fully native** (close a ~55K gap for PicoMan).
- **B. Remove the ceiling for tomorrow's** (GNR_6, Circle, bigger).

## 2. Where the bytes are

### 2a. Bytecode (100,773 bytes, 43,594 insns, 2.31 B/insn)

| opcode | bytes | % | why |
|---|---|---|---|
| `addr` | 29,240 | 29% | 5-byte absolute global address + 8-byte fixup each; nearly one per global load/store (5,848 sites vs 7,197 loads/stores) |
| `jfalse` | 9,084 | 9% | |
| `const64` | 8,892 | 8.8% | 9 bytes even for 0 and 1; MMINTEGER is 64-bit |
| `call` | 8,670 | 8.6% | 5-byte absolute + fixup; could be pc-rel16 |
| `const8`/`push`/`load32`... | | | already minimal |

### 2b. Native (per-op costs, from backend-thumb.c, agent-verified)

Dense already: fused local load 2-4B, ALU 2-6B, most of the common path
is fine.  The fat sites:

| site | bytes | where |
|---|---|---|
| double compound assign (`f = f + x` via eqop) | 34 inline | :1601-1614 |
| 64-bit eqop inline | 22-26 | :1545-1578 |
| every float(32) op, all via helper | 14-16 | :3430-3444 |
| unfused compare producing a value (`t_flagval`) | 14 | :631-637 |
| `SWITCH` compare chain | ~10 per case | :3519-3565 |
| `COPY` <=64 fully unrolled | up to 70 | :3465-3497 |
| stack guard, every function | 10 | :1000-1015 |
| forward-call trampoline residue | 3 nops/site | :3977-4055 |
| `ADDR` | 8 (movw/movt) + pair fixup | |

Also: r6 is now permanently zero (absolute addressing) and is a **free
callee register the backend never reclaimed** (bcrun.c:2819-2828).

## 3. The options, ranked

### Option 1 - native density pass (RECOMMENDED, do first)

Shrink the fat sites in backend-thumb.c.  Candidates, in effect order:
- double/64-bit eqop inline sequences -> a helper slot each (34->~12B,
  26->~12B).  These sites exist per compound assignment; games are full
  of them.  Perf cost: one blx to code that was mostly helper calls
  anyway (the double one already round-trips the DCP helper).
- `t_flagval` 14B -> IT-block or CSEL-free 8-10B form; or teach cc1/cc2
  that BASIC comparisons feeding `bool` can stay in flags (mmbc emission
  already made comparisons bare where possible).
- `SWITCH` chains -> a branch-table helper for >6 cases (ON GOTO/GOSUB).
- reclaim the 3-nop trampoline residue at link (emit short form, shift).
- share the 10-byte stack guard: guard only in functions that can
  actually recurse or allocate >N frame bytes, or BL a 2-byte guard stub.
- use r6 as a second scratch/cache register (it is free).

Honest estimate: **-10 to -20% of native bytes** (74K -> 60-66K for
PicoMan's 54 functions; all-native 195K -> ~160-175K).  Every program
shrinks; nothing gets slower; several sites get faster.  Measure with
THUMB_VERBOSE=1 per change (the instrument prints bc->native per
function), gate with fcctests + qemutests, board-verify per
one-change-at-a-time.

### Option 2 - raise the cliff and take main native (the perf prize)

Board cc2 today cannot translate any function over 48K of bytecode
(TMAX, arena-carved) and refuses over 40K of Thumb (THUMB_MAXFN).
PicoMan's main is 63.5K bc -> ~120K native.  With Option 1 landed and
Option 4's floor/pool trims, all-native PicoMan fits in SRAM:

    code ~160-175K + mem 60K + sym 7K + floor ~95-105K = 322-347K
    vs ceiling ~310-330K  -> tight; Options 1+4 together are required

Mechanics: raise TMAX (tbuf/tmap are already PSRAM-arena-carved;
the 1 MiB arena cap in backend-bcode.c:47-51 must rise with them),
raise CODEMAX (196,608 on board) and THUMB_MAXFN.  None of this is
algorithmic work.

**Payoff: the main loop of every game stops being interpreted - the
measured 2.7x.**  This converts the size problem into the performance
win the emission-findings queue already ranked first ("main-line native
wrapper is the next big lever").

Translator-side alternative if a single giant main stays awkward:
mmb2c could segment the main-line at label boundaries into chained
functions, each under the caps (GOTO = tail call; main-line variables
are already globals, so frames are nearly empty).  Keep as fallback -
it multiplies calls and complicates GOSUB/ON GOTO.

### Option 3 - code[] in a PSRAM arena (the ceiling remover; EXPERIMENT)

bcrun's loader does `code = malloc(h_code)` (bcrun.c:2342) out of the
SRAM process image.  Because thumb_link_calls makes same-object calls
RELATIVE and every other reference is fixed up at load, **the code blob
is position-independent as a whole**: bcrun could place it in a PSRAM
arena (PSRAMIOC_ALLOC, released on exit/exec automatically) and the
process would shrink by the whole code segment - PicoMan native drops
from ~315K to ~180K of SRAM, and the ceiling stops existing for code.

Why it is an experiment, not a plan: the tree's own words and numbers.
PC3-PSRAM-ARENA.md:76-82 - "The window is executable, so running a
binary from it would work.  Don't - instruction fetch that misses the
16 KiB XIP cache is a QSPI transaction, and that cache is shared with
flash XIP."  Measured analogues: MP3 working set in arena 2.91x->6.14x
slower and it put flecks on the display (PC3-MP3-PLAN.md:264-272);
tight loop from XIP flash 2.7x slower than RAM (bcrun.c:1688).  Nobody
has ever executed from PSRAM in this tree.

But the analogues are not the case itself: a translated BASIC program
spends much of its time in bcrun helpers, mm_* runtime and kernel
graphics code (SRAM/flash), not in straight-line fetch; a game loop
that fits the cache may pay little.  The experiment is cheap and
reversible: one alternative allocation path in bcrun (env-gated,
BCRUN_CODEPSRAM=1, malloc fallback), then the side-by-side authority:
PicoMan frame rate and the eclipse benchmark, SRAM vs PSRAM, with and
without PLAY MODFILE running (QMI contention).  Also note: fixups are
WRITES into the code blob; PSRAM writes go through the same cache
(write-back, XIP_CTRL_WRITABLE_M1) so no maintenance issue is expected,
but verify first-run correctness on the board, not just qemu.

If the numbers hold, this is the strategic answer for problem B, and
main-native stops being gated on Option 1/4 arithmetic.  If they do
not, we have spent a day and know for certain.

### Option 4 - shrink the floor and widen the pool (background trims)

Each is small; together 20-35K, enough to matter for Option 2:
- free sym+strtab after binding in the non-lazy path (kept for the
  whole run today; ~7K on PicoMan, 16.3K on eclipse).
- bcrun's 13.7K ELF reloc/dynamic LOAD segment: examine whether the
  rel.dyn pages can be handed back after relocation (loader change) or
  the segment slimmed at link.
- kernel .data trims -> pool boundary move (RAM and PROGPOOL only ever
  move against each other, memory_ram.incl): device_init strings 7.3K,
  plus candidates ITABSIZE 64->48 (-3.5K), NBUFS 20->16 (-2.1K),
  PTABSIZE 64->32 (-3K + swapaddr).  The stated valve is moving more
  kernel code to flash, per the excludes-list rules.
- TOTALMEM/PROGPOOL/RAM must move together; nothing checks they agree.

### Option 5 - bytecode compaction (do when next touching the format)

`addr` 29% of the bytecode is the headline: base+offset short forms
(gaddr16 3B, fused gload32/gstore32) save ~15-20K of bytecode and kill
~6K fixup records (48K of the .bc file); pc-rel call saves 3.5K + 1,734
fixups; const64s8/s16/s32 save ~7K.  Bytecode 101K -> ~70-75K.

Why it is ranked fifth for THIS problem: native size barely moves
(ADDR is movw/movt regardless), and if main goes native the interpreted
share shrinks to nothing.  Its real wins are load time (fixup count),
disk footprint, MAXFIX/table pressure, and any program still
interpreted.  The interpreter, bcdump (already stale - it cannot decode
some current ops and misreads native spans as nop runs) and t_span all
grow new cases; version-bump the object format and keep v1 decode.

### Rejected / deferred

- **Bytecode fallback as the answer**: retrograde, per the user.  It
  remains the safety net, and `cc` should apply it GRACEFULLY: instead
  of all-or-nothing BCODE_ONLY, the driver can estimate the load
  footprint (floor + h_code + mem + tables vs the pagemap ceiling) and
  set THUMB_BUDGET so translation stops when the estimate is reached -
  functions are translated first-come, so the small hot subs go native
  and only the tail drops to bytecode.  Cheap driver change; prints
  what it did.  Do this regardless as the stopgap that replaces the
  BCODE_ONLY=1 trap in the PicoMan manifest note.
- **mem[] / VM stack / data to PSRAM**: measured working-set penalties
  (2-3x) say no.  Bulk touched-once data only, and h_data is sprite
  source read every frame.
- **mmb_*.h statics moved into bcrun as libcalls**: saves ~30K per
  graphics program but grows the floor of EVERY process and couples
  runtime versions harder; revisit only if bcrun text ever moves to
  kernel flash.
- **bcrun text into kernel flash (PICOIOC_LIBM pattern)**: the deep fix
  for the 104.5K floor, with the shared-libm precedent (21% net WIN on
  eclipse because RAM saved beat flash slowness).  Big engineering,
  interpreter dispatch is exactly the hot-loop-from-flash case; only
  worth designing if Options 1-4 prove insufficient.

## 4. Recommended sequence

1. **Auto-budget in cc** (replaces the BCODE_ONLY=1 user trap; days).
2. **Option 1 density pass**, one emitter at a time, THUMB_VERBOSE +
   gates + board each step.
3. **Option 3 PSRAM experiment** in parallel (one env-gated bcrun
   change + benchmark day) - it decides whether Option 2 needs the
   Option 4 arithmetic at all.
4. **Option 2 main-native** by whichever route 2/3 opened.
5. Option 4 trims as needed; Option 5 with the next format change.

## 5. Corrections to existing notes (found during this review)

- pc3-v016-unreleased said "175K native + 66K program space ...
  bytecode-only is 101K".  Corrected numbers: 100,773 bytecode code
  segment; ~137.5K board-shape mixed code (54 fns native at 1.98x +
  main kept as bytecode); 175K was the HOST mixed object, which keeps
  dead bytecode (THUMB_RECLAIM defaults off host-side, on board-side).
- The "340K pool" figure survives in memory_ram.incl comments,
  PC3-PICOFROG-CRASH.md and FUZIX-PC3-MANUAL.md; live value is 336K
  (TOTALMEM=336, PROGPOOL 336k), practical single-process ceiling
  ~310-320K by largest_neighbour().
- Latent booby trap (not this problem, but found): the PSRAM newlib
  heap base (arena_pool_base() = 0x11014000) and the legacy /dev/hdc
  block device compute the SAME addresses; hdc is still registered and
  psram_disc_init memsets 2K there at boot.  Nothing uses hdc today;
  first user corrupts the heap.  Worth unregistering hdc or moving its
  base.

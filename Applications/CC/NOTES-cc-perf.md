# cc-perf branch: experiment log

Branch for performance/memory experiments out of REVIEW-2026-08-06.md,
each change A/B-gated by an env knob and measured on the hardware
before it is trusted.  Protocol per change: host suite (all.sh),
thumb/gate.sh five ways, qemudiff, mmb2c qemutests, ctest 165, then
the board.  Board numbers are RP2350B @ 378 MHz, the 2026-08-05 bcrun
(the object format and runtime are untouched on this branch so far).

Reference points: gcc -O2 cross 379,022 D/s; pc3-branch compiler
103,548 D/s (2026-08-05) / 102,842 measured this session at 400,000
runs; eclipse 2.312-2.317 s.

## 2026-08-06: direct [r4,#off] frame access  (THUMB_NOR4 disables)

LOCALn;LOADx becomes one load off r4; STOREx through a tracked frame
slot pops the address unread and stores off r4.  Commit 6f652e9b4.

## 2026-08-06: constant right operands  (THUMB_NOCFOLD disables)

PUSH;CONSTk;OP32 runs entirely in registers: add/sub/cmp immediates,
shifts by imm5, and/or/xor imm8, uxtb/uxth masks, constant built in
r2 otherwise.  Commit e784166a9.

## 2026-08-06: memory right operands  (THUMB_NORFOLD disables)

PUSH;LOCALn;LOADx;OP32 - and the ADDR global form - load the right
operand straight into r2 and run the operator without touching the
stack: `a+b` is ldr r2 / adds, the gcc shape.  The LOCAL offset is
rebased for the push that no longer happens (v-4, v < 4 declines).

## Board results, 2026-08-06 (all outputs verified identical)

| Dhrystone 2.1, 400,000 runs | D/s | vs base |
|---|---|---|
| base = pc3 shape (all knobs set) | 102,842 | - |
| + direct [r4,#off] | 110,487 | +7.4% |
| + constant folding | 116,776 | +13.5% |
| + memory right operands (branch default) | 118,264 | **+15.0%** |

Now 31.2% of gcc -O2, from 27.1%.  The rfold increment is small on
Dhrystone (+1.3%): cfold had already taken the constant shapes, and
much of what remains is call/string traffic, exactly as the review's
attribution said.  Diminishing returns on the operand-shape axis -
the next big slice is the library crossings.

Eclipse (DCP-double dominated, as predicted): 2.3165 -> 2.3022 s,
-0.6%.  Object sizes: dhry 15,910 -> 14,952 (-6.0%); eclipse
141,384 -> 133,160 (-5.8%) - the size win also buys back native-cliff
headroom (a function's native span must fit THUMB_MAXFN).

qemu wall time had predicted -9%; the board gave -11.9%.  qemu
understates memory-traffic wins - expect this again.

## 2026-08-06: string fast slots + word-wise strings + libm shims

One bcrun resend carrying three things (board now runs it;
/usr/bin/bcrun.prev is the rollback):

* ns_strcpy/ns_strcmp/ns_strlen: word-at-a-time (uint32_t, NOT
  unsigned long - the 64-bit-host trap struck AGAIN and libtest
  caught it), used by BOTH the interpreter's lib_* wrappers and the
  new version-4 helper slots 17-20, so the two paths cannot disagree.
* Translator: strcpy/strcmp/strlen/memcpy calls BL their slot
  directly - args load straight off the VM stack into r0-r2, no
  helper_call, no name dispatch.  They arrive as BC_CALL on a
  BC_SYM_LIB symbol (helper_call's tagged path), NOT as BC_LIBCALL -
  the profile had said so (libcall=0) and the first attempt hooked
  the wrong case.  Objects that use a slot carry BC_VERSION_NATIVE4;
  plain objects stay v3 and run on the old bcrun.  Old bcrun refuses
  v4 cleanly ("version 4, expected 1" - board-verified).
  THUMB_NOSTRSLOT=1 builds v3-compatible objects.
* bcrun_mm: pow/atan2/log10/sqrt shimmed through the shared kernel
  libm table (mfns 16/17/12/9).  The kernel table already exported
  all four - NO kernel flash was needed - bcrun just also called
  them directly, relinking ~5.1K of private copies per process.

## Board results, 2026-08-06 evening (all outputs verified identical)

| Dhrystone 2.1, 400k runs | old bcrun | new bcrun |
|---|---|---|
| pc3-shape object (v3) | 102,842 | 111,933 (+8.8% runtime alone) |
| all folds, dispatcher (v3) | 118,264 | 130,400 |
| all folds + v4 slots | - | **141,733** |

**Session total: 102,842 -> 141,733 D/s (+37.8%), 37.4% of gcc -O2.**
Compiler-side rewrites and runtime improvements compose; the runtime
half reaches every EXISTING object too: KnivD grains (old object,
BASIC) 37,600 -> **48,042 (+27.8%)**.

Eclipse: 2.3022 -> 2.3283 s (-1.1%) - the one debit: print formatting
now calls flash pow/log10.  Accepted against 5.1K/process; revisit
only if formatting-heavy programs complain.  bcrun image 82,980 ->
77,876 stripped.

Session cost note: two intermittent console wedges during transfers -
NOT the filesystem, NOT a panic; evidence and suspects in
platform-rpipico/NOTES-console-wedge.md, with dnull.py as the
discriminator.  Kernel-side fix is its own piece of work.

## 2026-08-06: inline BC_COPY (P4) and eager binding (R4)

* P4 (THUMB_NOICOPY disables): constant-length copies <= 64 bytes
  inline as ldr/str pairs - dst from the stack, src in A, A becomes
  dst, exactly the interpreter's case.  BCRUN_PROF on dhry: the last
  per-iteration runtime crossing (op c0) is gone; helper_call is 64
  for the whole 400k-run program, all startup.  Objects grow ~80B.
* R4 (BCRUN_LAZYBIND=1 restores first-call binding): libcall's strcmp
  chain - which never memoised, so every malloc/rand paid ~30 failed
  strcmps per CALL - is now lc_* table entries; every library symbol
  binds at load and the symbol + string tables are freed (~12K back
  on an eclipse-sized program).  A name the runtime does not provide
  is refused at load, program named, instead of exit(1) mid-run.
  Behaviour change to know about: a program that merely REFERENCES a
  missing function now refuses to load even if it never calls it.

Gates: all.sh 31 eager AND 31 lazy, thumb/gate 8/8, qemudiff 10/10,
mmb2c qemutests 17/17, ctest 165.

Board, 2026-08-06 late (cc2.p4 + bcrun.r4 installed; outputs
identical throughout): R4 is Dhrystone-neutral as expected (141,708
vs 141,733 - its chain calls were startup-only there) and trims the
eclipse to 2.3196 s; P4 delivered **165,688 D/s - +16.9%, four times
its estimate** - the per-iteration 48-byte struct copy through the
marshal + helper_op switch + memmove cost far more than modelled.
The board-compiled dhry.bc is byte-identical to the host build and
now finishes its 300,000 runs in under Dhrystone's two-second
reporting floor ("Please increase number of runs" - raise DHRY_RUNS
for on-board rate numbers).

**Session total: 102,842 -> 165,688 D/s, +61.1%, 43.7% of gcc -O2**
(from 27.1% this morning).  Every step board-verified with
byte-identical outputs.

## 2026-08-06: P5 (r4-pair elision), R1 withdrawn, R3 done

* P5 (THUMB_NORSKIP disables): the fused-window r4 pair goes when the
  window holds no LOCAL - the only builder that reads r4.  Virtual
  depth nets to zero across the window so every key/slot fact stays
  aligned with physical r4.  Mostly the 64-bit/double shapes the
  operand folds do not take; eclipse object -280 bytes, outputs
  identical, all gates green.  Board number pending.
* R1 WITHDRAWN - the loader already recycles the ELF reloc segment
  (stack window overlays its head, u_break = stacktop puts its tail
  in the heap).  The review's ~13.7K was an overcount; correction
  recorded in REVIEW-2026-08-06.md.  Leave-it-alone verdict.
* R3: prof_op/prof_lib allocated only under BCRUN_PROF - bcrun bss
  12,752 -> 10,712 (-2,040) in every process.

Board, 2026-08-06 night (bcrun.p5 + cc2.p5 installed, and the
console-hardened kernel flashed - see NOTES-console-wedge.md):
Dhrystone 166,834 D/s (+0.7% from P5; its dhry windows are few).
Eclipse 2.34s, P5-object vs pre-P5-object IDENTICAL to 4 digits -
P5's elided pairs are ~4 instructions around a ~60-cycle DCP call,
and the day's eclipse numbers (2.30-2.34) are runtime-noise wide.
P5 keeps its place on code size (-280B eclipse) and the dhry point.

**Session final: 102,842 -> 166,834 D/s, +62.2%, 44.0% of gcc -O2.**

## 2026-08-06 late: 64-bit and double compound assigns inline

User-spotted quick win: t_eqop inlined only widths 1/2/4, and
MMINTEGER is 8 bytes, MMFLOAT a double - so NO MMBasic counter ever
hit the inline path; every BASIC loop iteration crossed helper_eqop.
Now inline: 8-byte add/sub/and/or/xor (pre+post, the carry-pair
forms; mul/div/rem/shifts stay on the helper) and the double
pre-forms pluseqd/minuseqd/muleqd through the DCP slots (the address
stays on the stack across the call).  diveqd deliberately stays on
the helper: exec_eqop guards /= 0.0 to 0.0 where IEEE says infinity.

Board: eclipse 2.3400 -> 2.2772 s (-2.7%, the first real eclipse
gain from translator work; profile shows eqop crossings 0); grains
recompiled 48,142 -> 49,653 (+3.1%, +32.1% on the day); dhry
unaffected (already inlined at width 4).  All outputs identical;
full gate stack green including the eclipse 3-way referee.  cc2-only
- no bcrun or kernel change.

The same profile names the next frontier: helper_call 215,250 per
eclipse run, all mm_* dispatcher crossings.

## 2026-08-06 night: register caching of the hottest local (P6 v1)

The review's regalloc-lite, first increment: ONE 32-bit local per
function lives in r7 for the whole span (low register, full 16-bit
ALU, already in native_enter's clobber list - no bcrun change, no
version bump; the function saves it with push {r7,lr}, same two
bytes as before).  No spill state exists: memory never holds the
variable after the ENTER-time warm load, so eligibility is
everything - the collect walk classifies every LOCAL by FRAME
OFFSET (v minus a new TRUE stack-depth counter, t_vdepth), and any
use outside LOCAL;LOAD32 / pended-write / width-4 eqop escapes the
offset.  Reads become one mov; writes and compound assigns are
ANNOTATED BY CONSUMER OFFSET during classification and the emitter
keys on those offsets, never on tracker-slot survival.

That annotation design is the lesson of the round: the first cut
keyed the cached store on t_track.slot and the 3-way referee caught
Int_2_Loc = 9-for-13 in dhrystone main - an inner value push
overwrites the single slot fact, the store fell back to memory,
reads kept coming from r7.  The consumer-offset rework fixed it AND
widened eligibility (writes from call results, short-circuit RHS).
Two more classifier lessons: pendings must be created at the PUSH,
not the LOCAL (the by-depth sweep killed them at birth), and a
read span's leading LOCAL is a legitimate landing site (the loop
back-edge lands exactly there - checking it cost main its counter).

Knobs: THUMB_NOREGC (off switch), THUMB_REGCFN=name (bisection),
THUMB_REGCDBG=1/2 (choices / full tables).  Divergence anywhere =
bail to bytecode, never silent.  Scope note: 32-bit locals only, so
MMBasic's 64-bit counters are untouched - this round is for C code;
the 64-bit pair extension (r8:r9) is the follow-on.

Gates: all.sh 31, thumb/gate 8/8, qemudiff 10/10, mmb2c qemutests
17/17 (dhry 12/12 native), ctest 165, regbisect all-ok.  qemu wall
~1% (it flattens exactly what this targets: ldr->mov and the
in-register eqop).

Board, 2026-08-06 22:40 (same IR, on vs off, 2M runs): 166,864 ->
**172,248 D/s, +3.2%**, outputs identical; the off side reproduces
the P5-era number to 0.02%.  On-board compile with the regcache cc2
is byte-identical to the host build.

**Session total: 102,842 -> 172,248 D/s, +67.5% - 45.4% of gcc -O2,
INSIDE the review's predicted 45-58% band for the non-rewrite path.**

## Next candidates (from the review, in payoff order)

1. LOCAL;PUSH elision for stores whose slot is provably unread - needs
   DUP/SWAP/inlined-eqop consumer analysis first (they read the top
   slot); without it this is the silent-wrong-answer class.
2. The console-wedge kernel investigation (NOTES-console-wedge.md) -
   instrumentation plan is written; do it before the next release.
3. Stage-10e style re-profile of the eclipse under the new runtime -
   its remaining 2.3s is DCP arithmetic and formatting; measure
   before guessing further.

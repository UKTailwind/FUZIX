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
mmb2c qemutests 17/17, ctest 165.  Board pending (port busy with
local-compile testing): stage is cc2.p4 + bcrun.r4 in the CC dir,
board ladder in /tmp/ccperf-board (dhry-noic vs dhry-new isolates
P4; the r4 bcrun measures R4 + chain-bind speedup on old objects).

## Next candidates (from the review, in payoff order)

1. LOCAL;PUSH elision for stores whose slot is provably unread - needs
   DUP/SWAP/inlined-eqop consumer analysis first (they read the top
   slot); without it this is the silent-wrong-answer class.
2. Memory R1/R3 from the review (ELF reloc segment freed, lazy
   profiling arrays) - R2 and R4 are done above.
3. The console-wedge kernel investigation (NOTES-console-wedge.md) -
   instrumentation plan is written; do it before the next release.

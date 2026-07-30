# The Thumb backend, incrementally — rung 3 working plan

2026-07-30.  Decision taken: skip rung 2 (load-time translation) and go
straight to a real cc2 Thumb backend in mixed mode.  Rationale in
PLAN-native-backend.md; this is the execution plan.  The ladder doc's
job was "is this feasible in increments"; this doc's job is "what is
the next commit".

## The shape of the thing

One new file, `backend-thumb.c`, cloned from `backend-bcode.c` and
sharing everything except the code bytes: same object format (FBC1),
same data/literal segments, same symbols, fixups, labels and backpatch
machinery, same gen_* API driven by the same backend.c tree walker.
cc1 is untouched — target-thumb.c already models the ARM types.

Mixed mode is the increment mechanism: the emitter compiles a function
natively only if it meets the current coverage list, otherwise
re-emits it as bytecode.  Both kinds live in one object's code
segment; interpreted and native functions call each other freely.
Coverage widens one construct at a time, and at every point in between
the whole test suite runs and passes.

Native code keeps the interpreter's machine model deliberately:

* the VM stack in mem[] stays the stack — args, locals, temporaries.
  Callers push args the same way whichever kind of function they call,
  which is what makes mixing free.  The real machine stack is used
  only for BL return addresses within native code.
* program addresses stay 32-bit offsets into mem[]; native code keeps
  the mem base in a register and addresses with `[base, offset]`.
* A stays the value home: returned in r0/r1 (low/high, doubles as
  bit patterns) exactly as the interpreter's A.
* anything not yet inlined is a BL into a helper — and the helpers are
  the interpreter's own case bodies, extracted.  So the first native
  function body is correct by construction, and speed comes from
  replacing helper calls with inline Thumb, one opcode at a time.
  (This is rung 2's one good idea, relocated to compile time where the
  branch-remapping problem disappears — cc2 already has labels and
  fixups.)

Register plan (fixed for the life of the design, so helpers and
emitted code agree):

	r0/r1	A (value / value:high), scratch, arg to helpers
	r2/r3	scratch
	r4	vsp — the VM stack pointer, as a native pointer
	r5	helper vector base
	r6	mem[] base
	r7	scratch / future use
	sp	real machine stack: BL frames only

r4–r6 are callee-saved in AAPCS, so helpers written in C preserve
them for free.  vsp lives in a register as a native pointer, not an
offset, because it is touched constantly; it is written back to the
interpreter's `sp` (offset form) only at the boundaries.

## The seam: how the two worlds call each other

The frame layout is identical for both kinds of callee — that is the
whole trick.  A caller (either kind) pushes args on the VM stack, then
one slot for the return-pc, then transfers control.  A bytecode callee
BC_RETs through that slot; a native callee ignores it and its
trampoline pops it.  Offsets baked into BC_LOCAL / native equivalents
are the same either way.

Function entry gets a marker: the first code byte of a native function
is a new pseudo-op, `BC_NATIVE` (0xF0), followed by padding to 2-byte
alignment, then Thumb code.  Dispatch is then one byte-test:

* **bytecode calls X**: BC_CALL/BC_CALLA look at `code[X]`.  Marker
  absent: push pc, jump — exactly today.  Marker present: push the
  sentinel into the return-pc slot, load the register file (vsp, mem
  base, helper base), BLX into the Thumb entry, write A back, pop the
  slot, continue at the next bytecode.
* **native calls X**: every call site BLs a `call_target` trampoline
  with X in r0.  Marker present: tail-call it.  Absent: save pc, push
  sentinel, run the interpreter loop until it hits the sentinel RET
  (the existing 0xFFFFFFFF mechanism — made re-entrant by saving and
  restoring the loop's pc), restore, return with A.

Uniform indirection at every native call site costs a few cycles and
buys total freedom over which functions are native — no fixup
distinguishes the cases, and the emitter never needs to know what a
callee compiled to (it may not have seen it yet).  Direct BL between
known-native pairs is a stage-8 peephole, not a design requirement.

Helpers get no per-site fixups either: the loader fills a word table
(helper vector, base in r5) and native code does
`ldr r3, [r5, #idx*4]; blx r3`.  BC_LIBCALL from native code is the
same thing via a `native_libcall(idx)` helper, so printf, libm and the
mm runtime need nothing new.

Object format: unchanged except the marker byte inside code, and
`h_pad` becomes `h_flags` with bit 0 = "contains native code", so an
old interpreter rejects a mixed object at load with a clear message
instead of faulting on 0xF0 mid-run.

## Instruction-set notes (decided up front, so they never surprise)

* Thumb-2 as ARMv7-M: everything ARMv8-M mainline (M33) executes, and
  it also runs under qemu-arm on any Cortex-A.  No FPU instructions
  ever — doubles go through helpers, where the real work (aeabi
  soft-float inside bcrun) already happens; that is why the eclipse
  only stands to gain ~12% from floats but far more from dispatch.
* Constants via movw/movt, never literal pools — no pool placement,
  no 4-byte alignment islands, no pc-relative loads to get wrong.
* All code 2-byte aligned; BLX targets carry bit 0 (Thumb bit).
* Bounds checking: native loads/stores do not bounds-check (the
  interpreter's rd/wr do).  This is the one semantic divergence;
  the differential harness compares *output*, not faults, and a debug
  build can route loads/stores through helpers to restore checking.

## Stages

Each stage ends green: conformance suite + mmb2c suite, differential
where applicable, before the next begins.

**Stage 0 — prerequisite, independent: mm runtime into bcrun.**
Phase 1's outstanding move (mmb2c repo, fcc/PLAN-fuzix.md): the mm_*
runtime becomes native libcalls, translated programs shrink ~93K→~20K.
Wanted regardless, and it means the backend only ever compiles *user*
code — the runtime never round-trips through Thumb emission.

**Stage 1 — harness before backend.**  A `Makefile.qemu` building
bcrun with arm-linux-gnueabihf-gcc (static, -marm/-mthumb irrelevant
for bcrun itself; what matters is that emitted Thumb-2 runs in the
process) and a differential runner: every .bc executed under native
bcrun (host reference) and qemu bcrun, stdout byte-compared.  Run the
whole existing suite through it *interpreted* first — proves the
harness with zero new code in the loop.  (The Fuzix cross build can't
run under qemu-arm — Linux user emulation supplies Linux syscalls —
hence a third bcrun build.  Board remains the final gate.)
Setup, checked 2026-07-30: arm-none-eabi-as/objdump are present in
WSL; `qemu-user` and `gcc-arm-linux-gnueabihf` are not — one apt
install before stage 1 starts.

**Stage 2 — the seam, with no native code yet.**  BC_NATIVE defined;
h_flags; `bc_exec()` made re-entrant (save/restore pc); call dispatch
factored so BC_CALL/BC_CALLA go through one `call_target` path.  No
emitter changes; the marker is never generated.  Everything still
passes — this stage is pure refactor and lands first because it
touches the interpreter, which is trusted and tested.

**Stage 3 — one hand-written native function.**  Before cc2 learns
anything: hand-assemble (`arm-none-eabi-as` or a 20-line Python
encoder) the body of `int add2(int a, int b)` reading args off the VM
stack per the frame layout, patch it into a .bc by hand, run it under
qemu and *on the board*.  ~50 lines total, and it retires every real
unknown at once: the register file, the trampolines both directions,
frame-slot arithmetic, executable-RAM on Fuzix (checks any MPU
surprise), bit-0 interworking.  If anything about the design is wrong,
it is wrong here, cheaply.

**Stage 4 — backend-thumb.c v1: integer leaf functions.**  Clone
backend-bcode.c; emit per-function into a side buffer; if the function
trips anything outside coverage, throw the buffer away and re-emit
bytecode (the bcode emitter is cheap; per-function two-pass is fine).
v1 coverage — the integer core, each op a short inline sequence or a
helper BL, in this order, validating after each:

1. prologue/epilogue, BC_ENTER/LEAVE/RET equivalents, CONST via
   movw/movt, LOCAL addressing, PUSH/POP on vsp
2. LOAD/STORE at widths 1/2/4 (`ldrb/ldrsb/ldrh/ldrsh/ldr` + mem base)
3. add/sub/and/or/xor/shifts inline; mul inline; div/rem via helper
   (M33 has sdiv/udiv but the helper keeps v1 uniform)
4. compares materialising 0/1 (ite or branch pair), BOOL, jumps and
   conditional jumps to labels via the existing patch table
5. everything else — calls, 64-bit, floats, switch, struct copy,
   compound-assign helpers — trips the fallback

Exit: the integer subset of the conformance suite runs with those
functions native, differential-clean, and a loop-heavy microbenchmark
shows the expected multiple.

**Stage 5 — calls.**  Drop the leaf restriction: call sites become
`movw/movt r0, target-fixup; bl call_target`, BC_ARGS becomes an add
to vsp.  This is the stage where *most* real functions go native,
because BASIC-translated code is call-dense.

**Stage 6 — the wide types.**  64-bit ops (r0/r1 pairs where trivial —
add/sub/logic — helpers where not: mul/div/shifts); then the
double/float ops as pure helper BLs (BC_ADDD → `bl h_addd`); the
conversions likewise.  After this stage the eclipse compiles fully
native — first end-to-end timing against the 9.82 s interpreted mark.

**Stage 7 — the tail.**  BC_SWITCH (helper reusing the interpreter's
table walk), BC_COPY/BC_PUSHN (helper), the named compound-assign
libcalls, CALLA for function pointers.  Coverage list empties; the
bytecode fallback remains as the safety valve and the size lever.

**Stage 8 — measured peepholes, only now.**  In expected-value order:
compare+jfalse fusion (cmp + conditional branch, no 0/1
materialisation); constants folded into operands (add imm) via
gen_direct, which already presents them; redundant push/pop pairs
around sequential ops; direct BL for intra-module known-native
callees.  One peephole per commit, differential suite after each —
this is where a naive stack transcription usually goes subtly wrong,
so it waits until there is a trusted baseline to diff against.

**Stage 9 — the board proper.**  BIG_TABLES sizing for native output
(expect ~3–4× bytecode for covered code — CODEMAX and the on-board
cc2 limits need re-checking); a size policy for mixed mode (native
unless the code buffer would overflow, then largest-functions-first
back to bytecode); SD image refresh; eclipse timing.  Target from the
ladder doc: roughly 2–4 s.

## Baseline (2026-07-30, hosttest/bench.c)

The eclipse is deliberately NOT the yardstick: it lives in native
libm, so dispatch is a minority of its time (going native-runtime only
moved it 9.82 s → 9.19 s).  bench.c is what dispatch actually costs -
five phases, integer/call/memory bound, no float in any hot loop, each
printing a 32-bit checksum so every run is a differential test against
gcc -O2 as well as a timing.  Run both ways with hosttest/bench.sh;
the .bc is 3,387 bytes and travels to the board in two seconds.

| phase | host gcc -O2 | host bcrun | PC3 bcrun |
|---|---|---|---|
| sieve (8191×10)   | <1 ms | 67 ms  | 3.93 s |
| fib(27)           | <1 ms | 33 ms  | 2.69 s |
| shellsort (2000×5)| <1 ms | 52 ms  | 3.76 s |
| xorshift (200k)   | <1 ms | 184 ms | 8.30 s |
| byte-rev (4K×200) | <1 ms | 98 ms  | 6.94 s |

Board ≈ 45-82× the host interpreter, phase for phase.  These are the
numbers the backend is judged against: at the expected 3-8× for the
naive emitter, the board column lands around 0.5-2.5 s per phase, and
per-phase movement says which templates pay (rng = pure ALU, rev =
loads/stores, fib = call boundary, sieve/sort = mixed).

**Dhrystone 2.1** (netlib dhry-c in hosttest/dhry/, definitions
ANSI-fied for cc1 — it has no old-style parameter declarations — plus
a TIME_US microsecond clock and a fixed run count; bodies untouched,
output self-validates):

| build | Dhrystones/s |
|---|---|
| host gcc -O2 | 74,970,000 |
| host bcrun | 289,700 |
| **PC3 bcrun** | **3,800** (263 µs/run, ≈ 2.2 DMIPS) |

A native M33 at this clock is good for roughly 300-600k Dhrystones/s,
so the ceiling above the interpreter is ~100× on this workload; the
naive emitter's 3-8× leaves plenty for stage 8's peepholes to chase.
`bash hosttest/dhry/dhry.sh` rebuilds all three.

## Status

* **Stage 0 done** 2026-07-30: mm runtime native in bcrun (bcrun_mm.c),
  eclipse 9.19 s on the PC3, t6 dirs free, programs ~4.7× smaller.
* **Stage 1 done** 2026-07-30: Makefile.qemu + hosttest/qemudiff.sh,
  10/10 differential-pass including the eclipse.  The harness earned
  its keep immediately: an odd h_data put the whole bss segment - and
  its "aligned" int64 arrays - at an odd address (SIGBUS under qemu,
  silent on x86, latent on the board).  bssbase now rounds to 8 in the
  loader and mem[] itself carries aligned(8) instead of linker luck.
* **Stage 2 done** 2026-07-30: BC_NATIVE marker + version 2 objects
  (bytecode.h), unified call_target dispatch, re-entrant bc_exec,
  native_enter with the r4/r5/r6 register file.  All suites green,
  conformance still 165, bench times unchanged - the dispatch test
  costs nothing measurable.
* **Stage 3 done** 2026-07-30, on hardware: hand-written Thumb add2
  (hosttest/nat/) patched over its bytecode stub by natpatch.py, run
  through the real dispatch under qemu AND on the PC3 - plain sums
  where the stub shows +1000000, nested calls feeding native results
  to native args across the VM stack.  Executable RAM under Fuzix
  confirmed (no MPU surprise), Thumb bit right, frame parity right,
  x86 host refuses version-2 objects cleanly.  Eclipse regression
  clean at 9.08 s.

* **Stage 4 v1 done** 2026-07-30, checkpointed on hardware every few
  opcode groups (CP-A frame/const/jump/ret, CP-B stack/locals/loads/
  stores/addresses, CP-C 32-bit ALU + inlined compound-assign
  helpers, CP-D compares and conditional jumps - probes preserved in
  hosttest/thumb/, gate.sh runs all four, four-way agreement demanded:
  gcc -O2, host-alias, qemu-native, qemu-forced-bytecode).

  The design ended up better than planned: rather than a second tree
  walker, backend-thumb.c translates the function's just-emitted
  bytecode span at gen_epilogue - the bytecode is the single source
  of truth, labels are still symbolic, and bailing is free.  The
  BC_NATIVE marker carries a bytecode-alias field so mixed objects
  run on ANY host (x86 interprets the original span; BCRUN_BYTECODE=1
  forces it on ARM = free A/B).  Every i++ style compound assign,
  previously a named libcall, is inlined.

  **Hardware results, integer leaf functions (bench.c on the PC3,
  same binary A/B):**

  | phase | bytecode | native | speedup |
  |---|---|---|---|
  | sieve | 3847 ms | 50 ms | 77× |
  | shellsort | 3659 ms | 74 ms | 49× |
  | xorshift | 8176 ms | 60 ms | 136× |
  | byte-rev | 6757 ms | 118 ms | 57× |
  | fib(27) | 2644 ms | (bytecode: calls) | - |

  The "3-8×" estimate was off by an order of magnitude: the
  interpreter's bounds-checked byte-assembled memory path made board
  dispatch far more expensive than modelled, and these loops now run
  at essentially native M33 speed.  Conformance 165 throughout;
  fcctests 9/9; qemudiff 10/10 every checkpoint.

* **Stage 5 done** 2026-07-30 (CP-E): calls.  A native call site
  loads a trampoline from the helper vector, passes its vsp in r1,
  and BLXes; helper_call dispatches to native_enter for a native
  callee or a fresh bc_exec activation for a bytecode one - whose
  sentinel is also the frame-parity slot, so the layouts line up by
  construction.  BC_CALL targets travel through the literal pool with
  a flagged fixup (flag 1 = value slot): the loader must never
  mistake a pool word for a BC_CALL operand, and a target that
  resolves to a library symbol becomes a tagged index the trampoline
  turns back into a libcall.  General libcalls (printf, the mm
  runtime, libm) go through a second trampoline; the 64-bit/floating
  eqop names bail the function - they pop a slot inside C, which
  would desync the caller's r4.  Functions now save lr (push {lr} /
  pop {pc}).

  Two bugs found by the gates, both in dispatch plumbing, neither in
  emitted code: (1) after a native callee that itself makes calls,
  the global sp is whatever the deepest helper sync left it - the
  dispatcher must restore the slot level before popping (leaf
  callees, which every earlier checkpoint used, never touch it);
  (2) at -Os, gcc's IPA analysis looked straight through the asm
  "memory" clobber, decided native_enter never writes sp, and
  deleted the callers' restore - the board failed where the -O2 qemu
  build passed.  native_enter now preserves sp itself and declares
  sp/A/pc/mem as explicit "+m" asm operands.  The -Os/qemu repro
  (arm-linux-gnueabihf-gcc -Os) is the debugging path of record for
  any future board-only failure.

  **Hardware: fib(27) 2644 -> 382 ms (6.9×); Dhrystone 3,808 ->
  8,288/s (2.2×) with only 9 of 18 functions native** - struct
  assignment and switch (stages 6-7) block the rest, including the
  measurement loop's own Proc_1.  cpe probe: recursion, mutual
  recursion, function pointers through CALLA, printf from native
  code - all on the PC3.  The eclipse stays 0/34 native (every
  function touches doubles) until stage 6.

Next: stage 6 - the 64-bit and floating ops as helper calls (and the
loads/stores/pushes for two-slot values), which is where the eclipse
moves.  Then 7 (switch, struct copy), 8 (peepholes - direct BL for
known-native callees will lift fib well past 6.9×), 9 (board sizing
policy + SD refresh).

## Floating point engines (decided 2026-07-30, checked against the SDK)

The RP2040's boot-ROM float library does not exist on the RP2350: the
SDK's fast path there is double_aeabi_dcp.S - hand assembly for the
DCP, the double-precision coprocessor in the M33s - shipped in the
binary, nothing in ROM, nothing copied from ROM.  Our Fuzix userland
links the v8-m.main/nofp libgcc, so bcrun's doubles today are plain
soft-float in its own text.  64-bit integers involve no ROM or
coprocessor anywhere: inline adds/adcs/umull, libgcc __aeabi_ldivmod
for divide.

Stage 6 stays engine-agnostic - helper BLs remove the dispatch around
FP whatever implements the arithmetic.  The engine upgrade is a
SEPARATE work item benefiting interpreter and native equally: import
the SDK's DCP __aeabi_d* routines into bcrun (soft-ABI libm lowers
all its arithmetic to those symbols, so libm accelerates
transparently), plus the kernel side - CPACR enable and lazy
per-process DCP/FPU context save, mandatory now the port preempts.
MicroPython's 8.77 s eclipse already rides the DCP via the SDK; this
is the gap-closer.  The single-precision FPU (FPV5-SP) has the same
kernel prerequisite and much less value here: MMFLOAT is double, so
translated BASIC touches almost no single-precision.

**DONE 2026-07-30.**  Library/libs gains dcp_double.S and
dcp_conv_m33.S - the SDK's DCP aeabi routines and the pure-M33
64-bit conversions, regenerated verbatim by dcp-sync.sh under a
small local macro prelude (the __aeabi_l2d/ul2d/f2d set must come
too: libgcc bundles them in one object with dadd, so any unresolved
member drags all the soft doubles back in and collides).  These
override libgcc purely by link order, so EVERY userland relink gets
them - bbcbasic included.  Kernel: CPACR CP4 enable plus one RCMP to
clear the stale-at-reset engaged flag (miss that and the wrappers'
save path restores garbage state forever).  No context-switch save
needed: the SDK wrappers are self-saving against interrupted use.

**Results, PC3, all digit-identical to soft-float/host:** eclipse
9.19 s -> **7.16 s** (past MicroPython's 8.77, runtime still
interpreted bytecode); bbcbasic 1M-double-arith loop 15 s -> 9 s,
100k SIN*COS 3 s -> 2 s.

The hunt found a real latent compiler bug: cc2 emitted the
double-typed BOOL on the *integer* result of a double comparison.
Integer 1 read as a double is 5e-324 - a denormal - so exact
soft-float said "true" by accident for years, and the DCP, which
flushes denormals to zero, said "false": every compared condition in
every bytecode program died.  Fixed twice over: BOOLD/LNOTD (and the
float forms) in bcrun are now bit tests - identical IEEE semantics
including NaN and -0.0, denormal-exact on any engine - and the
backend's T_BOOL/T_BANG treat relational operands as the integers
they are (is_boolish).  Debugging path of record: soft-float relink
(ar d the dcp objects) bisects engine vs everything else in one run.

Known DCP limitation, accepted: genuinely denormal user values
compare as zero.  MMBasic arithmetic does not produce them in
practice; anything that cares can BCRUN_BYTECODE=1... no - can link
soft.  Documented here so nobody rediscovers it the hard way.

## Order of risk retirement

Stages 1–3 are small and kill the unknowns (harness fidelity,
re-entrancy, executable RAM, the ABI).  Stage 4 is the long grind but
each step is mechanical against backend-bcode.c open in the other
window.  Nothing after stage 3 should produce a surprise that isn't a
plain emitter bug, and every emitter bug is caught by a differential
run naming the first diverging output line.

## What is deliberately not here

No register allocation, no FPU use, no direct-threaded dispatch
improvements to the interpreter (rungs 1–2 declined), no attempt to
make native objects loadable by old bcruns.  And no board-side cc2
work until stage 9 — development runs host-side, qemu for execution,
the board for milestones.

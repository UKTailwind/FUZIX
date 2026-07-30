# From bytecode to machine code — the migration ladder

2026-07-30.  Analysis locked in ahead of a decision on next steps.
**Decision taken later the same day: rung 3 mixed mode, skipping rung
2 — the staged execution plan is PLAN-arm-backend.md.**
Context: the mmb2c pipeline runs real programs on the PC3 under bcrun
(solar eclipse 9.82 s vs MMBasic's 12.5 s, MicroPython's 8.77 s), and
the intention is native ARM output eventually.  The question was
whether that can be reached in increments or is a start-from-scratch
backend.  Answer: **incremental, on a three-rung ladder, and rung 3 is
not from scratch.**

## How the interpreter works (what any migration must preserve)

A stack machine with one 64-bit accumulator, deliberately C-shaped:

* `A` (int64) holds the current value.  Doubles travel as their bit
  pattern in A; on the stack a double is two 4-byte slots.
* `mem[]` is the program's whole data world — data, bss, heap, stack —
  32-bit offsets, bounds-checked.  Code lives outside it (`pc` indexes
  a separate buffer): a program cannot address its own instructions.
* ~144 opcodes in a switch: loads/stores at widths 1/2/4/8, int and
  double arithmetic (BC_ADDD is an instruction), compares, PC-relative
  16-bit jumps (code is position independent), call/enter/leave/ret.
* `BC_LIBCALL`: unresolved externs bind **by name** at load and
  dispatch into native C — printf, libm, files, and soon the whole mm
  runtime.  No linker; the object is header/code/data/fixups/symbols.

Cost model: ~15–30 cycles of dispatch around each op's real work —
except doubles, whose real work is an aeabi soft-float call native
code would make too.  That is why the eclipse is only 12% behind
MicroPython: float-heavy code lives in native libm/soft-float already.
Dispatch elimination pays most on integer, string and loop-heavy code.

## The ladder

**Rung 1 — tighten the interpreter (days, ~1.5–2×).** Computed-goto
dispatch, superinstructions for common pairs.  No format change.
Probably skip: rung 2 subsumes it.

**Rung 2 — translate at load time (genuinely incremental).** The
loader already walks every instruction applying fixups.  Extend it to
emit, per opcode, a Thumb sequence into an executable buffer — on day
one just `BL handler` for everything (subroutine threading; the
handlers are the interpreter's own case bodies, so it is correct by
construction).  Then inline templates **one opcode at a time** — push,
load32, store32, add, compares, jumps — validating each step against
the interpreter with the existing suites.  Flat address space on this
port: no mmap/W^X ceremony, just jump into RAM.  Expect 3–8× on
integer code.  The kit's `backend-threadcode.c` shows the author has
been down this road — a reference exists.

**Rung 3 — a real cc2 ARM backend.  Not from scratch:**

* cc1 needs nothing — `target-thumb.c` (the ARM type model) is what
  compiles everything today.
* `backend.c` (947 lines) does the tree walking shared by every
  backend; a backend implements a **31-entry `gen_*` API**.
* `backend-bcode.c` (1,244 lines) implements that API for the same IR
  and is the line-by-line reference; the kit's dozen other backends
  (8086, Z80, 6809…) supply idioms.
* **No assembler or linker to write**: the bcode object format carries
  Thumb-2 machine code in its code section unchanged — fixups, symbols
  and loader already exist.  bcrun's loader shrinks to "load, fix up,
  jump"; the LIBCALL boundary becomes a BL into the same runtime.
* The conformance suite + mmb2c tests + differential runs against the
  interpreter validate every emitter decision.

A naive emitter keeping the stack discipline (no register allocation,
A in r0/r1) is a few thousand lines — the same order of work as the
bcode backend was.

**Rung 3's own increment: mixed mode.**  Emit native code per
*function*, falling back to bytecode for anything the young backend
cannot handle; both live in one object sharing the stack discipline
and runtime.  Start with integer-only leaf functions, widen coverage.
This is how it lands in bits.

## Recommendation

Prototype rung 2's subroutine threading briefly (it derisks the
executable-memory story), but put the real effort into rung 3 with
mixed mode.  Expected eclipse time native: roughly 2–4 s (libm is
already native; the VM overhead is what disappears).  String/loop
heavy BASIC gains more.  Note `Makefile.armm4` is an empty stub — no
prior ARM backend exists anywhere in the tree or the kit.

Prerequisite either way: phase 1's runtime-into-bcrun move (see the
mmb2c repo's fcc/PLAN-fuzix.md) — it shrinks programs ~93K→~20K and
is wanted regardless of how code gets executed.

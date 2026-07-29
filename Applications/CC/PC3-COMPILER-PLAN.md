# An on-target compiler for the PC3

Plan, 2026-07-28. Replaces THUMB-BACKEND-PLAN.md, which planned a
conventional backend before the survey below changed the shape.

## Decisions taken

* **Scope is single-file programs.** Utilities and small programs that
  can be compiled on the machine itself. Explicitly *not* libm, BBC
  BASIC, or the kernel — see "What we cannot compile".
* **No assembler and no linker.** Neither exists for ARM and neither is
  needed at this scope.
* **A bytecode intermediate layer**, so C, and later BASIC and Pascal,
  share one path to code generation.
* **Interpret first, translate later.** The bytecode runs before any
  code generator exists.
* Instruction set: Thumb. See "Thumb-1 or a Thumb-2 subset" — the
  original decision was Thumb-1 only and there is a good argument for
  relaxing it slightly.

## Why not start from another compiler

Surveyed and rejected, on two filters that apply before licence or
quality:

**Cortex-M executes Thumb only** — there is no A32 state. Almost every
small ARM C compiler was written for Raspberry Pi-class Linux and emits
A32, including TCC's `arm-gen.c`. Retargeting one is the same work as
writing a Thumb backend from scratch.

**Processes are 255K, flat, resident in SRAM.** PSRAM is swap, not
address space. A monolithic compiler must fit code, data, symbol tables
and heap in one process; TCC's binary alone is 100–300K before it reads
any source.

| candidate | verdict |
|-----------|---------|
| MicroCC (ezulabs) | closest on paper — Thumb codegen, runs on a Pico. One commit, no licence, no documented subset, **JIT only**. Read it, don't build on it. |
| gfwilliams/tinycc | author: *"only works very minimally"*; prologs hacked, conditional branches unreliable |
| TCC | A32 output, monolithic. Fatal on both filters |
| lcc | C89, no ARM backend, needs an assembler, restrictive licence |
| PCC | C99 and BSD-licensed, but A32 and workstation-shaped |

The conclusion that matters: **FCC's multi-pass split is its real asset
and the one thing no alternative has.** `cc0 → cc1 → cc2` as separate
processes is why cc1 fits in 49.7K. That design was for 64K 8-bit
machines, and a 255K flat process is closer to that world than to Linux.

## What FCC already gives us

More than expected. `Operations.md` is not prose — it is a numbered,
language-neutral bytecode ISA for an accumulator+stack machine:

    0  ltlt    16 32          38 assign   8 16 32
    2  gtgt    16u 32u 16s 32s 41 deref   8s 8u 16 32
    6  cceq    16 32          45 negate   16 32
    8  band    16 32          47 funccall
    10 mul     16 32          50 constant 8s 8u 16 32
    20 plus    16 32          60 loadl
    ...                       62 exit  63 jump  64 jfalse  65 jtrue

and `backend-bytecode.c` (647 lines) already lowers C onto it. The
proposed bytecode layer is largely specified and partly built.

Also usable as-is: `cc0` (tokeniser, with a software IEEE-754 encoder so
it bootstraps on machines with no FP), `cc1` (recursive-descent parser,
reported to parse the whole Fuzix codebase), and the `target-<cpu>.c`
split that confines machine knowledge to ~114 lines.

## What we cannot compile, and why the scope is what it is

FCC targets **C89 minus deliberate omissions**. Checked against this
tree:

* **No 64-bit `double`.** Every `target-*.c` remaps `DOUBLE → FLOAT`;
  cc0 encodes 32-bit IEEE only. **81 files in `Library/libs` use
  `double`** — that is libm. BBC BASIC uses it in five files.
* **No bitfields** (omitted by design). Used by
  `Kernel/platform/platform-rpipico/console.c`, `util/fat.c`,
  `util/stty.c`, `Library/libs/resolv.h`.
* **No struct/union passing or returning.**
* **Locals have function-wide scope, not block scope.** This is not old
  C, it silently changes the meaning of valid C89.
* **Constants live in `unsigned long`** (`tree.h:15`). Cross-compiling
  on x86-64 that is 64 bits; self-hosted on 32-bit ARM it is not, so
  64-bit constants become unrepresentable exactly when self-hosting.
* Identifiers significant to 14 characters; 30 struct members; 50 enum
  constants; 128 case labels; `char` defaults unsigned; `-32768` is a
  known mistyping bug.

None of this is fixable cheaply, and all of it pushes against choices
Alan Cox made deliberately. Hence: single-file programs, and gcc remains
the toolchain for anything real.

## Architecture

    C (FCC cc0+cc1)      BASIC        Pascal
              \            |            /
               \           |           /
                  bytecode (Operations.md, widened to 32-bit)
                 /                          \
        interpreter                   Thumb translator
        (small, built first)          (later, for speed)

Two properties make this the right shape:

1. **The bytecode is executable before any code generator exists.** An
   interpreter for that op set is a couple of KB. C programs *run* as
   soon as the front end works, and every later front end inherits that
   for free. The code generator is never on the critical path.
2. **Native code can arrive incrementally.** Translate the hot opcodes,
   interpret the rest — exactly the mechanism `TARGET.md` already
   describes for replacing helpers one at a time.

## What double still needs

The wide-literal work is done (token stream, cc0 accumulation, lex,
primary), so `long long` literals now match gcc. `double` needs three
more things, in this order, and the first is the real work.

**1. A double literal encoder in cc0.** `convert_fix32()` is
single-precision from end to end and cannot be reused as-is:

* input is a 32.32 fixed point pair, so the value carries at most
  ~56 bits before conversion even starts;
* it normalises to a 28-bit intermediate, then to a 24-bit mantissa;
* it assembles `sum |= (exp << 23) & 0x7F800000` — IEEE single;
* the decimal fraction table is 24-bit with **nine entries**, stopping
  at 1e-9, and the last few are already degenerate (`0x2A`, `0x4`, 0).

So `0.123456789123456` arrives as about `0.12345679` — and digits past
the ninth decimal are dropped before precision is even the question.

The constants are worth writing down, because they are not obvious.
`exp += 22` then `exp += 128` is `exp + 150`, which is `23 + 127`: the
mantissa is normalised with bit 23 set, so the value is
`1.xxx * 2^(23+exp)` and the stored exponent is `23 + exp + bias`. For
double the equivalent is **`exp + 1075`** (`52 + 1023`), with the
mantissa normalised to bit 52, the exponent field at bit 52, the valid
range 1..2046, and infinity `0x7FF0000000000000`.

The pre-multiply normalisation is 28 bits so that a `* 10` cannot
overflow 32; the double equivalent is 60 bits so it cannot overflow 64.

Also needed: a fraction table with ~64-bit entries extended to about
1e-19, and `frac` accumulated at 64 bits in `dec_format`.

**2. Stop remapping the type.** Every `target-*.c` has
`if (type == DOUBLE) return FLOAT;` in `target_type_remap()`, so DOUBLE
never survives the frontend. target-thumb.c has to keep it once the
backend can handle it.

**3. Float and double opcodes.** Same pattern as the 64-bit integer
set: the accumulator and slot handling already exist, so this is
largely new opcodes plus conversions to and from the integer types.

gcc is the oracle throughout - `hosttest/optest.sh` will report a
mis-encoded constant immediately.

## Phases

### Phase 0 — ABI and bytecode freeze

32-bit `int`/`long`/pointer, 64-bit `long long` where representable,
`char` signedness chosen deliberately (gcc's ARM default is unsigned;
our gcc builds pass `-fsigned-char`). Decide whether float is in scope
at all — `Makefile.armm0` currently passes `-DNO_FLOAT`.

Freeze the bytecode as a **binary encoding**, not the text form
`backend-bytecode.c` currently prints for the byte assembler. Drop the
16-bit size variants; this is a 32-bit machine.

### Phase 1 — 32-bit frontend — **DONE 2026-07-28**

Delivered: `target-thumb.c`, the `CPU_armm0` block in `target.h`,
`Makefile.host` (native cc0/cc1 for development) and `hosttest/`.

Verified sizes, by scaling each `sizeof` by 1000 so it is unmistakable
in the object stream: char 1, short 2, **int 4, pointer 4, int[2] 8**
(the 16-bit model gives 2/2/4 for the last three). Type codes in the
debug dump show `int` as 0x20 (CLONG) and `int *` as 0x21.

Differential corpus over `Applications/util`, 121 files: 1 clean in both
models, 119 with identical errors, 1 (`fforth`) where the 32-bit model
reports *fewer* errors, and **0 regressions**. A width-sensitive torture
test compiles with 0 errors.

One real 32-bit bug found and fixed in shared code: `helper_type` in
`backend.c` had `case UINT:` alongside `case ULONG:`, which collide once
int is 32-bit, and mapped pointers to `USHORT`. Now uses concrete widths
and `UINT` for pointers — identical on 16-bit targets, correct on 32-bit.

All four passes cross-build for ARM. Loadable sizes: cc0 33K, **cc1
48.8K**, cc2 32K, copt 12.5K, against a 255K process — so cc1 leaves
~200K for its own tables, and the size question is settled.

Known, pre-existing, not ours: `long long` declarations are rejected
("type conflict") in both models, and `fforth` segfaults cc1 in both.

### Phase 1 as originally planned

`target-thumb.c` (~120 lines, model it on `target-z80.c`) and a
`CPU_armm0` block in `target.h`; the makefiles already pass
`-DCPU_$(USERCPU)`. `TARGET_MAX_INT/UINT/PTR` are used in only seven
places (`enum.c:95`, `frontend.c:899-908`, `initializer.c:30,60,156`).

**No existing target has ever set `CINT != CSHORT`.** Assume bugs and
find them here, where they are cheap.

Success test: cc1 parses a body of C with 32-bit types without
asserting.

### Phase 2 — bytecode emitter — **DONE 2026-07-28**

Delivered: `bytecode.h` + `BYTECODE.md` (the frozen encoding),
`backend-bcode.c` (cc2 emitting binary objects) and `bcdump.c` (a
disassembler, which is also the reference decoder the interpreter and
the translator get written against).

`Makefile.armm0` now builds the real thing: cc1 with the ARM type model,
cc2 emitting bytecode. Loadable sizes cc0 33K, cc1 48.8K, cc2 109K
against a 255K process — cc2's bss is mostly its fixed output buffers.

Over `Applications/util`, 115 of 123 files compile all the way to
bytecode with **no crashes** (5 errors, 3 unpreprocessable).

Three things worth knowing, each of which cost a debugging round:

* **`gen_node` must never return 0.** The shared `make_node()` falls
  back to `helper()`, which `printf`s the helper name — harmless for a
  backend emitting text, fatal for one writing a binary object, because
  the name lands in the middle of the code stream. Anything not
  generated inline becomes a `BC_LIBCALL` using FCC's own helper names.
* **`T_CLEANUP` must be handled in `gen_direct`, not `gen_node`.** It
  carries the function's return type, so the byte count to discard is in
  `n->right->value`. Miss it and the pushed arguments stay in the
  stack-depth accounting and every epilogue fails with "sp".
* **`gen_segment` matters.** Ignore it and an uninitialised global gets
  a data symbol while its storage is counted in bss.

Symbols carry names in a string table: `BC_SYM_LIB` entries have to be
matched against whatever runtime the interpreter provides, and an index
alone would not say which function was meant.

### Phase 2 as originally planned

Widen `backend-bytecode.c` (it opens `/* For now assume 8/16bit */`) and
switch it to the frozen binary encoding. Output is a file the loader can
read — no assembler, no linker.

### Phase 3 — interpreter — **DONE 2026-07-28**

`bcrun.c`. **C compiles and runs.** Verified on the host:

    sub(a,b) + a for-loop summing 1..10, returning 55-13   ->  exit 42
    strings, %s, %d with negatives, %x, putchar,
    and recursive fib(0..9)                     ->  0 1 1 2 3 5 8 13 21 34

The 42 matters twice over: it is arithmetic and control flow, and `sub`
is order-sensitive, so it also proves the argument mapping. Arguments
turn out to be pushed right to left, so the first parameter is nearest
the stack pointer.

Built for ARM Fuzix too: bcrun 76.9K loadable (the 64K VM address space
dominates), bcdump 8.8K, against a 255K process. Not yet run on
hardware — that needs the SD card rewriting.

Design decision worth keeping: **the VM has its own address space**, one
`mem[]` array holding data, bss and the stack, with a program pointer
being an offset into it. Program pointers stay 32-bit whatever the host
is, so the same interpreter runs on the development machine and on the
PC3. Code lives outside that space and is not addressable, so a function
pointer is a code offset.

Four things that bit, all now fixed and worth not repeating:

* **The frame convention was wrong in the emitter.** `gen_frame` added
  the frame size to the pushed-depth counter, as the other backends do,
  which put every local *above* the return address in the caller's
  frame. A local at frame offset v lives at `sp + v + pushes`; the size
  belongs only in `frame_len` for arguments. The symptom was an infinite
  loop, not a clean failure.
* **Calls to undefined names are library calls**, and the compiler
  cannot know that when it emits the call because the definition may
  come later. The loader rewrites `BC_CALL` to a `BC_SYM_LIB` symbol
  into `BC_LIBCALL`; the spare two bytes become NOPs so the instruction
  keeps its length.
* **Literals need their own buffer.** The frontend declares a data
  label, switches to the literal segment, emits the string, then
  switches back — one counter put `char *msg = "..."` and its string
  both at offset 0.
* **Sign extension.** Everything entering A or the stack must be sign
  extended from 32 bits on a 64-bit host, and the return sentinel then
  has to be masked back to 32 bits or it never matches.

**Verified on hardware 2026-07-28** once the console UART was fixed.
`bcrun` (11.5K stripped) and the sample objects were sent over the
console with `uusend.py`, and all four samples produce output identical
to the host: a Sieve of Eratosthenes (46 primes below 200), an RPN
calculator, a strings/struct exercise, and the recursion/printf test.

Four defects found and fixed by those samples, none of which the earlier
toy tests had exercised:

* **`switch` was never implemented** in the interpreter. The table is
  `[count]` then `(value, label)` pairs with the default label last, all
  resolved by fixups.
* **Case values are emitted at the switch expression's own width.**
  `switch (c)` on a char gave one-byte values and a five-byte stride,
  which read as garbage and jumped into hyperspace. C promotes the
  switch expression to at least int, so the emitter now widens case
  values to a word and the table is uniform.
* **Compound assignment ignored the operand width.** `+=`, `++` and the
  rest became library calls carrying no width, so the runtime did a
  32-bit read-modify-write on whatever it was given. It works until a
  carry leaves the object: `char a = 255, b = 2; a += 1;` wrapped `a`
  correctly *and* incremented `b`. The calls now carry width and
  signedness (`pluseq1u`, `shreq4s`).
* **`printf` shared one static buffer** between the format string and
  its `%s` arguments, so the format was overwritten mid-scan. It also
  ignored width and flags, which threw the argument numbering out.

Current limit: the runtime library is `putchar`, `puts`, `printf`
(`%d %u %x %c %s` with flags and width), `exit` and the
compound-assignment helpers. Programs needing file I/O, `str*`/`mem*`
or `malloc` will not run until that grows.

### Phase 3 as originally planned

A bytecode interpreter as a normal Fuzix program. **At the end of this
phase you can compile and run C on the machine.** Slow, but real, and
it is the milestone that makes everything after it optional.

Success test: `hello.c`, then progressively larger single-file programs
from `Applications/util`, compiled and run on hardware.

### Phase 4 — Thumb translator

Bytecode → Thumb, opcode at a time, hottest first. Recommended form:
**translate at load time into RAM**, not to a file. With the real
addresses in hand there is no relocation, no file format and no linking
to design. For single-file programs in a 255K process this is very
reasonable, and it is what MicroCC does.

If an ahead-of-time file is wanted later, `Kernel/syscall_exec32.c`
(a.out plus custom relocations) already exists, uncompiled, and the
kernel can dispatch on magic.

Note `PROGLOAD` is now fixed at **0x20030600** (pinned pool at
0x20030000 plus `UDATA_SIZE` 1536), so absolute code would work. Do not
bake that in — that coupling is exactly what the 2026-07-28 stack
alignment bug was made of.

### Phase 5 — more front ends

BASIC and Pascal emitting the same bytecode. This is where the layer
pays for itself.

## Thumb-1 or a Thumb-2 subset

The decision was Thumb-1 (ARMv6-M) for simplicity. Having worked through
the encodings, **that choice makes the implementation harder, not
easier**, and it is worth revisiting:

| problem | Thumb-1 | with a little Thumb-2 |
|---------|---------|----------------------|
| load a 32-bit constant | literal pool; `LDR Rd,[PC,#imm8*4]` reaches only **+1020 bytes forward**, so pools must be flushed constantly and placed where control flow cannot fall into them | `MOVW`/`MOVT`, two instructions, **no pool at all** |
| conditional branch | **±256 bytes**; beyond that, invert the condition and jump over an unconditional `B` (±2KB); beyond that a `BL` veneer | `B<cond>.W` ±1MB |
| divide | no instruction; helper call | `SDIV`/`UDIV` |

Literal pools and branch veneers are the two fiddliest parts of Thumb-1
code generation, and both simply vanish. The machine is definitely an
M33; restricting to ARMv6-M buys portability to hardware we do not have,
at the cost of the hardest code in the project.

**Recommendation: allow `MOVW`/`MOVT`, `B<cond>.W` and `SDIV`/`UDIV`.**
Everything else Thumb-1. If portability to an RP2040 ever matters, those
four are a small, well-isolated set to provide fallbacks for.

## Risks

1. `CINT != CSHORT` has never been exercised by any target. Phase 1
   exists to find out how bad that is before anything depends on it.
2. The op set is **C-shaped** — C types, C pointer semantics. Pascal
   sets and nested procedures, and BASIC strings, do not map directly
   and will need runtime calls or op-set extensions. Phase 5 will be
   less free than it looks.
3. Interpreter performance may be disappointing enough to make phase 4
   feel obligatory rather than optional. Measure before promising.

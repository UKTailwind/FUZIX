# Adding a Thumb (Cortex-M33) backend to the Fuzix Compiler Kit

Review and plan, 2026-07-28. Target: the PC3 (RP2350B) able to compile
C for itself.

## What is actually here

The kit is five programs chained by the `cc` driver (`ccfuzix.c`):

    cpp -> cc0 -> cc1 -> cc2 -> copt -> as -> ld

* **cc0** (`frontend.c`) tokenises and numbers identifiers.
* **cc1** parses to trees. Machine knowledge lives in one small file,
  `target-<cpu>.c` — the Z80 one is **114 lines**: type sizes,
  alignment, argument size, pointer arithmetic type, register hints.
* **cc2** walks the trees and emits assembler. `backend.c` (940 lines)
  is shared; `backend-<cpu>.c` is the machine part.
* **copt** is a peephole pass driven by `rules.<cpu>`.

Backends that exist: 6502, 65c816, 6803, 6809, 8070, 8080, z80,
bytecode, threadcode, ee200, nova3, plus `backend-default.c`.

**There is no ARM backend anywhere.** What exists is scaffolding:

| file | state |
|------|-------|
| `Makefile.armm0` | builds the passes with the **Z80** target and backend |
| `Makefile.armm4` | empty stub (`all:` / `clean:`) |
| `rules.armm0` | one comment line, no rules |
| `../assembler/Makefile.armm0` | empty stub |

`Makefile.armm0` says so itself: *"Stage 1 experiment: the compiler
passes hosted on the ARM Fuzix machine, generating Z80 code (no thumb
backend exists yet)."* It proved the passes fit and run in a Fuzix
process. It produces Z80 assembly, which is of no use on the PC3.

### Sizing the job from the existing backends

    backend-default.c    294 lines   the "generate bad code" starting point
    backend-6809.c       976 lines   a decent two-index-register machine
    backend-z80.c       2538 lines   mature, heavily optimised
    backend.c            940 lines   shared, not rewritten per target
    target-z80.c         114 lines   all of cc1's machine knowledge

So a *working* Thumb backend is on the order of 1000–1500 lines, and a
*first* one that mostly emits helper calls is a few hundred. The
abstract machine is deliberately weak: one working register wide enough
for any type, a stack, locals and arguments on the stack. That maps
onto Thumb trivially (r0 as the working register, sp, r7 as frame
pointer) — the model costs performance, not correctness.

## The three real problems

The backend itself is the *easy* part. Three things are harder, and two
of them are not code generation at all.

### 1. The kit is a 16-bit compiler

`target.h` is a single shared file with no per-CPU conditionals:

    #define TARGET_MAX_INT   32767L
    #define TARGET_MAX_PTR   TARGET_MAX_UINT     /* 65535 */
    #define CINT             CSHORT

Pointer *size* is already per-target (`target_sizeof` returns 2 on Z80,
would return 4 for us), so that part is fine. The constants above are
not: they are used for enum range checks (`enum.c:95`), integer literal
typing (`frontend.c:899-908`) and initialiser string handling
(`initializer.c:30,60,156`). Seven sites.

The decision that matters: **`int` must be 32-bit on this target.** Not
for C conformance — 16-bit `int` is legal — but because the existing ARM
userland, libc and kernel headers are all gcc-built with 32-bit `int`.
A 16-bit-`int` FCC could not share a single header or `.a` with them.

So `target.h` needs `#ifdef CPU_armm0` (the makefiles already pass
`-DCPU_$(USERCPU)`) selecting `CINT = CLONG`, 32-bit maxima, and 32-bit
`TARGET_MAX_PTR`. Whether the frontend is genuinely clean about
`CINT != CSHORT` is the first thing to test — no existing target has
ever set it, so assume bugs and budget for them.

### 2. There is no ARM assembler

`Applications/assembler` is a multi-pass assembler: `as0`–`as4` shared,
plus `as1-<cpu>.c` (parse/encode) and `as6-<cpu>.c` per CPU. Thumb
would need `as1-thumb.c` / `as6-thumb.c` and an `OA_ARM` arch code.
Thumb-2's mixed 16/32-bit encodings, PC-relative literal pools and
branch ranges make this meaningfully harder than the 8-bit targets it
was built for.

### 3. There is no ELF, and the kernel wants ELF

`ld.c` (1390 lines) reads and writes only the native Fuzix object
format (`MAGIC_OBJ`). It contains no ELF code at all. The kernel's
loader (`syscall_execelf32.c`) requires `-pie -static` ELF with a
DYNAMIC segment and `R_ARM_RELATIVE` relocations.

There is a way out that is easy to miss: **`Kernel/syscall_exec32.c`
already exists and is not compiled in.** It loads "a.out with a custom
relocation format" via `plt_relocate`. Teaching the kernel to accept
both formats — dispatching on magic — is far less work than writing an
ELF producer, and lets the native toolchain keep the object format it
already has.

Also note the native `ld` cannot read the gcc-built ELF `libc*.a`. A
natively-compiled program needs a Fuzix-format libc, which means
rebuilding `Library/libs` with FCC once the backend works. That is the
real "self-hosted" milestone, and it is downstream of everything else.

## Recommended plan

Deliberately ordered so each phase is independently useful and testable,
and so the risky toolchain work happens after the backend is proven.

### Phase 0 — decide the ABI (no code)

Fix and write down: 32-bit `int`/`long`/pointer, 64-bit `long long`,
`char` signedness (gcc's ARM default is *unsigned*; `rules.armm0` builds
already pass `-fsigned-char` for bbcbasic, so choose deliberately),
struct layout and alignment, argument passing. **Recommendation: match
AAPCS exactly** so FCC output can link against gcc-built objects and
call the existing libc. Anything else creates a second incompatible
world on the same machine.

### Phase 1 — `target-thumb.c` + 32-bit frontend

Add the `CPU_armm0` block to `target.h` and a ~120-line
`target-thumb.c`. Success test: cc1 parses the Fuzix codebase with
32-bit types without asserting. Flush out the `CINT != CSHORT` bugs
here, where they are cheap to find.

### Phase 2 — `backend-thumb.c`, emitting **GNU as** syntax

Copy `backend-default.c`, retarget the directives, jumps, prologue and
`gen_helpcall`. Emit gas syntax and assemble with `arm-none-eabi-as`
and link with the existing `elfexe32.ld`. This sidesteps problems 2 and
3 entirely for now and gets real binaries onto the PC3 early.

Native code the obvious nodes first, per `TARGET.md`: `T_CONSTANT`,
`T_LABEL`, `T_NAME`, `T_LOCAL`, `T_ARGUMENT`, then `gen_push`. Everything
else falls through to helpers. Write the helper runtime
(`supportarmm0/`) in gas assembly.

Success test: compile and run `hello.c`, then progressively larger
`Applications/util` programs, on hardware.

### Phase 3 — quality

`rules.armm0` peephole rules, `gen_direct`/`gen_shortcut` for constant
and stack forms, replacing the hottest helpers with inline Thumb. This
is where the code stops being embarrassing. Measurable against the
existing gcc builds of the same utilities.

### Phase 4 — the native toolchain (the expensive one)

Only now: `as1-thumb.c`/`as6-thumb.c`, ARM relocations in `ld.c`, and
the loader decision from problem 3. Enabling `syscall_exec32.c`
alongside the ELF loader is the recommended route.

### Phase 5 — self-hosting

Rebuild libc with FCC into Fuzix-format archives; compile the compiler
with itself on the PC3. `PROGSIZE` is now ~255K per process and cc1 was
49.7K back when the limit was 64K, so size is no longer the constraint
it was.

## Phyllosoma — worth reading, not worth importing

`kmorimatsu/phyllosoma_TLS13` (MachiKania) is a BASIC compiler in C that
emits **native Thumb machine code on-device** for RP2040/RP2350. It is
genuine prior art for the thing we are least sure about: generating ARM
code in a small machine with no assembler.

Two caveats found on inspection:

* **Not separable.** There is no encoder file. The file list is
  `compiler.c`, `statements.c`, `operators.c`, `functions.c`, `run.c`,
  `value.c`, … — instruction emission is woven through the statement and
  expression compilers. There is nothing to lift out.
* **Licence.** It is LGPL 2.1; FCC is GPLv3 (`README.md:210`). LGPL 2.1
  §3 does permit taking code under GPL, so importing is *legally*
  possible with notices preserved — but it would need Alan Cox's
  agreement for anything going upstream, and given the first point there
  is little to import anyway.

Its real value is as a worked reference for phase 2/4 questions: literal
pool placement, branch fixups, how generated code calls into a C
runtime, and the fact that an ARMv6-M (Thumb-1) subset is sufficient —
which is worth knowing, because a Thumb-1-only backend is materially
simpler to write and still runs on the M33.

## Biggest risks

1. **`CINT != CSHORT` has never been exercised.** No existing target
   sets it. Phase 1 exists to find out how bad that is before anything
   depends on it.
2. **Thumb-2 assembler complexity** if phase 4 is attempted at full
   scope. Restricting the backend to a Thumb-1 subset would cut this
   down sharply, at some cost in code size and speed.
3. **Two toolchain worlds.** Until libc is rebuilt natively, native
   compilation and cross compilation cannot link against each other.
   Matching AAPCS in phase 0 is what keeps that gap closable.

# mmb2c under Fuzix — the plan, and where it stands

2026-07-30.  Goal: `mmbc prog.bas` on the PC3 under Fuzix — translate
MMBasic to C, compile with the Fuzix C compiler (FCC) to PC3 bytecode,
run under bcrun.  Speed is not the near-term concern: the intention is
to add machine-code output later, at which point translated programs go
native.

## Phase 0 — prove the pipeline end to end on the host: **DONE**

`.bas → mmb2c --fcc → gcc -E → cc0 → cc1 → cc2 → .bc → bcrun`

Result: **8 of 9 tests pass byte-for-byte**, including the 3,213-line
solar eclipse predictor, which reproduces the August 2017 eclipse to
the digit.  t6 fails only its directory section (see "decisions" below).

Run it: `bash fcc/fcctests.sh` (needs `make -f Makefile.host` done in
the FUZIX `Applications/CC` tree first).

### What this repo gained

* `--fcc` flag: no compound literals — array bounds descriptors hoist
  to `static const` tables at file scope, by-ref numeric arguments go
  through `mm_byref_f/i()`.
* `mm_byref` is a **stack**, wound back by the same `mm_mark`/
  `mm_release` the string temporaries use (both tops packed in the one
  mark word, so generated code is unchanged).  It was a ring first, and
  the eclipse found the flaw: a by-ref argument must stay live for the
  whole duration of the call it feeds, and `eclprint` makes more than
  16 nested by-ref values before returning.
* Runtime portability, unconditional: own calendar arithmetic
  (`mm_civil_from_days`, no more `gmtime`/`struct tm`), `mm_parse_hms`
  (no more `sscanf`), no `snprintf`.  Under `-DMM_FCC`: `mm_eof` by
  seek-to-end (bcrun files have no pushback), `PAUSE`/`TIMER` via the
  interpreter's `time_us` (31-bit — TIMER wraps after ~35 min, a known
  phase 0 limitation).
* `fcc/include/`: math.h, stdlib.h, ctype.h, time.h, stdint.h for the
  bytecode world.  math.h and the conversions carry **full prototypes**
  — `pow(10, n)` must convert its ints to double at the call site;
  unprototyped, the interpreter reads 8 bytes where 4 were pushed.

### What the compiler tree gained (FUZIX pc3 branch)

bcrun: the mathematics library (19 functions, table driven, native
speed), strtod/strtol family, atof/atol, rand/srand/time, rename,
llabs — and a real bug: `lib_eqop` never handled the size-8 forms, so
`x /= 16` on a long long read and wrote only 32 bits.

cc1/cc2, genuine bugs found by this work (each bisected to a minimal
repro against gcc as oracle):

1. **Constant comparison folding**: `T_GT` and `T_GTEQ` in the folder
   computed `<` — copy-paste of the `T_LT` case.  `4 >= 5` folded to 1.
2. **Cast of a sub-array**: `(char *)two[1]` loaded the row's first
   four bytes as the pointer.  make_rval left a sub-array flagged LVAL;
   the cast then treated it as an object.
3. **Pointer-to-array parameter**: `type_canonical` decayed
   `char (*a)[257]` as if it were an array, losing the row size — every
   `a[i]` addressed row 0.  Guarded with the same PTR-vs-dimensions
   test make_rval uses.

Limits raised for real programs: NUM_NODES 100→512, MAXLABEL 16→256
(GOSUB return switches manufacture labels freely), backend CODEMAX
32K→128K, DATAMAX 16K→64K, MAXSYM 2048→4096, STRMAX 8K→32K.
Makefile.host: bcrun links -lm; note it has **no header dependencies**
— after touching compiler.h, `rm -rf host-armm0`.

## Decisions still open

* **Directories (t6)**: MKDIR/RMDIR/CHDIR/CWD$/DIR$/FILES are stubbed
  (`-DMM_NO_DIRS`).  Needs a small libcall set; DIR$ wants a dedicated
  `dir_open`/`dir_next` pair (one scan at a time, exactly MMBasic's
  model) so no structs cross the VM boundary.  Pattern matching stays
  in the runtime.
* **Runtime placement**: phase 0 concatenates mmb_runtime.c with the
  program (no linker in the bytecode world) — ~45K of every .bc is the
  runtime, recompiled every time.  Phase 1 moves the mm_* entry points
  into bcrun as native libcalls: smaller binaries, faster strings, and
  the on-board compile only sees the program.  BYTECODE.md explicitly
  blesses this.  Keep mmb_runtime.c as the reference implementation —
  gcc builds still use it, and xcheck must keep proving the digits.
* **Uncast 2D-array argument**: after fix 3, passing `g` where
  `char (*)[257]` is expected wants the explicit cast (which mmb2c
  emits).  Teaching type_pointerconv the decay equivalence is upstream
  work, not urgent.

## Numbers so far (host, Ryzen)

solar eclipse compute section: bcrun 0.10 s, gcc -O2 native 0.002 s —
the interpreter costs ~50× on float-heavy code.  The comparison that
matters — bcrun on the PC3 against MMBasic's 12.5 s and MicroPython's
8.77 s on the same chip — needs phase 1's board bcrun (the board's
current binary predates the math libcalls).  MMBasic is an interpreter
too; that race is fair, and machine-code output later is the real win.

## Phase 1 — the board

1. Rebuild bcrun for Fuzix/ARM with the new libcalls; refresh the SD.
2. Solar eclipse timed on the PC3.  This is the benchmark that decides
   how loudly the manual talks about speed.
3. mm_* runtime into bcrun as native libcalls (the placement decision
   above), xcheck digits proven on-board.
4. The directory libcall set; t6 to 9/9.

## Phase 2 — the translator in C

Native ARM Fuzix binary (~256K process), not bytecode.  The harness
exists first: the C translator must emit byte-identical output to
mmb2c.py over the whole suite.  ~6–8k lines; the Python stays as the
reference.

## Phase 3 — integration

`mmbc` driver (translate → cc → run), SD image, manual chapter, and
the eclipse compiled on the board as the acceptance test.

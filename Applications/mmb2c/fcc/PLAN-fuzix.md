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

## Numbers

**On the PC3 (RP2350B @ 378 MHz), solar eclipse, same program, same
inputs, output verified correct:**

| environment | time |
|---|---|
| MMBasic (interpreted BASIC) | 12.5 s |
| MicroPython port | 8.77 s |
| mmb2c → FCC bytecode → bcrun, runtime as bytecode | 9.82 s |
| **mmb2c → FCC bytecode → bcrun, runtime native** | **9.19 s** |

Faster than the MMBasic interpreter already, 12% behind MicroPython —
with a plain bytecode interpreter and the whole mm runtime still
compiled *as* bytecode.  Host reference (Ryzen): bcrun 0.10 s vs
gcc -O2 native 0.002 s, ~50× interpreter cost, so the machine-code
backend remains the big win when it comes.

## Phase 1 — the board: bcrun landed 2026-07-30

Done: board bcrun rebuilt with the math libcalls and installed over
serial along with the fixed cc1/cc2; the eclipse runs correct on
hardware.  Getting there found **three Fuzix libc bugs** (all fixed in
the FUZIX tree, all upstream candidates):

* **`tan()` returned `-cot()` for every argument** above 2^-27: tan.c
  is musl-shaped (odd = 0 means tan), the __tan.c kernel was FreeBSD's
  (k = 1 means tan).  Found because the moon's right ascension came
  out 35° wrong while its declination was exact — `sin(π−x) = sin(x)`
  pointed straight at the one function the math probes hadn't covered.
* **`strtod()` built fractions back to front** — ".25" parsed as 0.52
  (`fp/10 + digit/10` instead of a shrinking scale), and the exponent
  loop added one per digit ("1e2" = 1000).  ELKS code from 1995.
* **`cosh`/`tanh` missing from the libc build** — cosh.c existed but
  was never in SRC_LM; tanh.c didn't exist at all (written new,
  expm1-based).  tanf is still absent and __tandf still has the
  FreeBSD flag convention — flagged, not fixed, no callers today.

Board memory reality: bcrun is one 256K Fuzix process holding its own
code, the VM data space AND the loaded bytecode + tables.  The VM gives
way on the board: MEMSIZE 48K (`BIG_TABLES` keeps 128K on the host).
The loader's mallocs are now checked — unchecked they surfaced as
"short code at pc 0".  Board cc2 keeps the small table sizes for the
same reason.

Debugging method note: instrument off the hot path.  A Print inside
moon() — called every objective evaluation — flooded the console file,
wedged the board, and cost a reflash + fsck.  The working pattern:
call the SUB once from the main line, print, `End`.

### Runtime into bcrun: DONE 2026-07-30, on hardware

The mm_* runtime is native inside bcrun (`bcrun_mm.c` in the FUZIX
tree includes verbatim copies of mmb_runtime.c/.h — masters stay HERE,
`fcc/sync-runtime.sh` copies them over).  The runtime's own code runs
on native pointers untouched; ~135 small wrappers convert VM offsets
at the boundary, and the only program-addressable state — the scratch
pool and the by-ref pool — is carved out of VM memory by the loader.
Names bind once per run through a cache (libbind), not per call.

What it took, beyond the wrappers:

* **cc2 now honours data/bss alignment** (gen_data_label ignored its
  align argument).  The interpreter never cared — byte-at-a-time — but
  native code dereferencing a VM double on ARM does.  The Thumb
  backend will want this anyway.
* **DATA moved to four parallel primitive arrays** (`mm_data_init4`):
  int, double and int64 read identically on both sides of the VM
  boundary, where MMDataItem's padding was each compiler's own
  business.  The string table stays a VM offset — its elements are
  32-bit VM pointers, unreadable through a 64-bit host pointer.
* Fuzix libc islands: strtoll/strtoull route to bcrun's own 64-bit
  parser (Fuzix's stops at 32), remove() → unlink().

Results, all 9 tests byte-exact on host AND the eclipse on hardware:

* **t6 is 9/9** — the POSIX directory code that had to be stubbed in
  the bytecode world (no opendir libcall) simply runs natively.  The
  planned dir_open/dir_next libcall design is moot.
* programs shrink ~4.7× (t-tests 74K → 15.7K; the eclipse 163K →
  103K, its own 3,200 lines are genuinely most of it)
* **eclipse on the PC3: 9.191 s** (was 9.82 s with the bytecode
  runtime; MMBasic 12.5, MicroPython 8.77).  Float-heavy, so the
  gain is modest — string-heavy programs gain far more.
* `RTBC=1 fccbuild.sh` still builds the old concatenated shape for
  differential debugging.

Still to do in phase 1:

3. Refresh the SD image with the new binaries (they live only on this
   card until then).  Note the on-card cc2 and bcrun were updated over
   serial 2026-07-30 — a .bc compiled on-board by the new cc2 needs
   the new bcrun and vice versa is fine (alignment only adds padding).

## Phase 2 — the translator in C

Native ARM Fuzix binary (~256K process), not bytecode.  The harness
exists first: the C translator must emit byte-identical output to
mmb2c.py over the whole suite.  ~6–8k lines; the Python stays as the
reference.

## Phase 3 — integration

`mmbc` driver (translate → cc → run), SD image, manual chapter, and
the eclipse compiled on the board as the acceptance test.

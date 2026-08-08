# MMBasic coverage review and expansion plan — 2026-08-07

A comprehensive review of MMBasic coverage in the mmb2c → cc → bcrun chain
on Fuzix/PC3, against the PicoMite 6.03.01 firmware keyword tables
(byte-identical to V6.03.00).  Part 1 audits what is covered today and
whether each piece sits in the right build category.  Part 2 allocates
everything not covered.  The plan at the end turns the findings into
sections to be implemented one at a time, each confirmed on the physical
hardware with regression tests before the next begins.

The universe: ~200 distinct command keywords and ~165 function/token
keywords in the firmware, plus ~630 second-word sub-verbs.  After removing
variants that cannot apply to this machine (WebMite networking, LCD-panel
drivers, USB-variant-only gamepad/keyboard commands), the applicable set
is ~190 commands and ~160 functions.  Covered today: ~50 command keywords
and 71 functions, with the mmbc C translator tracking the Python at
byte-identical parity.

## The build categories, and what each actually costs

1. **Fully inbuilt** — inline C from the translator, or a call into
   `mmb_runtime.c`.  Since phase 1 the runtime is compiled natively INTO
   bcrun (26.9K of its 67.8K text) and reached by named libcalls at load.
   Cost: zero per program file, but it inflates the ~104.5K floor that
   every `.bc` process pays out of the 340K pool — including plain C
   programs that never touch BASIC.  Native speed.
2. **Header of static functions** — `mmb_gfx*.h` / `mmb_gpio.h` style.
   cc1 generates nothing for a file-scope static that nothing names, so a
   program pays only for what it calls.  Cost lands only on programs that
   use the feature.  Runs as cc-generated code: slower than the native
   runtime.
3. **Spawned program** — `playmp3`, `saveimage`, `loadimage`, `SYSTEM`.
   Costs neither bcrun nor the caller a byte; runs native gcc ARM and may
   hold the FPU.  Must be cross-compiled off-board; costs a fork of a
   ~200K process plus two `sync()`s.
4. **Never** — cannot be honest here, or the OS already does the job.
5. **Large and tenuous** — possible in principle, rationale weak.

So "more in 2/3 = smaller programs" is right in the sense that matters —
process footprint — but the lever for a 1→2 move is bcrun's floor, not
the `.bc` file, and every 1→2 move trades native speed for cc-code speed.

Three real limits of the category-2 mechanism (compiler evidence:
`body.c:492-502`, `lex.c:42-87`, `hosttest/deadstatic.sh`):

1. **DCE is name-counting, not reachability.**  A static that names
   itself, or is named by another (even dead) static, survives.
   `mmg_circle` is self-recursive, so once `mmb_gfx.h` is included the
   whole circle machinery (~230 lines) is generated even for a TEXT-only
   program.  Only leaf entries can be dropped.
2. **DCE applies to functions only.**  `mmg_eo[481]`/`mmg_ei[481]` cost
   ~1.9K of BSS in any program that includes the graphics header.
3. **The translator's include gate is one flag per header, not per
   feature.**

None of these need compiler changes to fix: split the header per feature
and gate each include on its own flag.  That is Section 1 of the plan.

---

## Part 1 — is what is covered today in the right category?

### Correctly placed

* **Category 1, universal** (~825 lines): scratch/by-ref pools, core
  string ops, IntToStr/FloatToStr, PRINT and column tracking, numeric
  helpers, error/END, heap, GOSUB stack.  Every program touches these and
  native speed matters.
* **Category 1 for structural reasons**: the kernel crossings —
  `mm_plot`/`mm_fill`/`mm_gpio` and every `/dev/sys` ioctl — because the
  on-board cc has no `ioctl`.  The `mm_run_*` spawn plumbing is the
  category-3 enabler.
* **Category 2**: CIRCLE, TEXT, MAP MAXIMITE/GRAYSCALE geometry;
  SETPIN/PIN.  Right model, weakened by the one-flag include (fixed in
  Section 1).
* **Category 3**: PLAY MP3 (background, kernel arbitrates the device,
  only FPU-licensed process), SAVE IMAGE, LOAD IMAGE, SYSTEM.

### Misplaced — category-1 code that is pure arithmetic, used by only some programs

~1,100 of `mmb_runtime.c`'s 3,897 lines have no kernel dependency:

| feature | ~lines | note |
|---|---:|---|
| LONGSTRING family (21 fns) | 199 | pure buffer arithmetic |
| INKEY$ escape decoder | 175 | the 73-line ESC table walk is pure; the one-byte read stays in bcrun |
| date/time civil arithmetic | 159 | epoch↔civil is pure; only "now" needs the OS |
| FORMAT$ | 134 | |
| SORT | 95 | hot-loop candidate — see caveat |
| DATA/READ/RESTORE | 89 | |
| BIN2STR$/STR2BIN | 87 | |
| BYTE/TRIM$/FIELD$ | 65 | |
| MATH() reductions | 59 | hot-loop candidate — see caveat |
| ARRAY SET/ADD/SCALE | 36 | |

Also movable: file management (KILL/RENAME/COPY/MKDIR/RMDIR/CHDIR/DIR$/
FILES + wildcards, ~486 lines — bcrun already exposes the POSIX calls by
name), and graphics geometry that is not a crossing (`mm_line`'s
Bresenham, `mm_pixels`, the FRAMEBUFFER block, ~400 lines net of ioctls).

Caveat: a 1→2 move recompiles the feature as cc code, slower than native.
Move the cold, size-heavy items; keep SORT and the MATH() reductions
native, since they are exactly what gets called in hot loops.  The prior
bcrun review's biggest item (R5, shrink resident bcrun) is attacked by
these moves at far lower risk — worthwhile once >2 concurrent `.bc`
processes matter.

### Other part-1 findings

* **BOX and BLIT do not exist.**  COVERAGE.md claimed both translated;
  there is no dispatch anywhere.  Doc corrected 2026-08-07; BOX itself is
  Section 2 work.
* Docs were stale the other way too: INKEY$, PIN/SETPIN, PIXEL, MAP, the
  whole graphics set, PLAY, the spawns, MM.HRES/VRES, the heap split and
  `--fcc` mode were all in the code but not the docs.  Corrected
  2026-08-07.
* PLAY VOLUME as an emitted-only-when-used static is the right pattern.

---

## Part 2 — everything not covered, allocated

Categories: **1** inbuilt · **2** header+DCE · **3** spawned · **4**
never/not needed · **5** large/tenuous.  **†** = also needs new kernel
surface (ioctl or /dev node).

### Language core

| feature | cat | rationale |
|---|---|---|
| TYPE/END TYPE, STRUCT members, STRUCT( | 1 | C structs; translator-only except the known `a.b` tokenizer change.  Largest single win left |
| ON ERROR SKIP/IGNORE, MM.ERRNO, MM.ERRMSG$ | 1 | cross-cutting error flag or setjmp in the runtime |
| REDIM [PRESERVE] | 1 | easier than COVERAGE.md said: arrays already live in the mm_heap PSRAM block |
| CALL(fname$, …) by name | 1 | generated name→pointer dispatch table |
| RUN prog$, CHAIN | 3 | `mm_run_exec` of another compiled `.bc` — fresh process, fresh variables, near-exact semantics |
| EXECUTE, EVAL | 4 | need the interpreter |
| OPTION ESCAPE, OPTION ANGLE | 1 | translator-only |
| remaining OPTION sub-keywords (~120) | 4 | firmware/board configuration; the kernel and Fuzix own it |
| CSUB/END CSUB | 4 | embedded ARM blobs; write C and SYSTEM it instead |
| TRACE LIST EDIT NEW SAVE LOAD AUTOSAVE LIBRARY XMODEM YMODEM HELP | 4 | REPL/editor duties; fzsh, levee, fm, uusend do these as OS programs |
| SETTICK, ON KEY, ON PS2, INTERRUPT/IRETURN | 5 | SIGALRM-based SETTICK possible but semantics diverge from between-statement interrupts; silent divergence is worse than the honest error |

### Strings, data, math

| feature | cat | rationale |
|---|---|---|
| ARRAY SLICE/INSERT | 2 | pure index arithmetic |
| LONGSTRING BASE64, MATH BASE64 | 2 | pure, small |
| LONGSTRING AES128, MATH AES128 | 2 | pure; on demand |
| MATH matrix/vector/quaternion (M_* V_* Q_*), C_* complex, CRC8/12/16/32, INTERPOLATE, WINDOW, SHIFT, SLICE, SINC, CHI, CORREL, CROSSING, MAGNITUDE, DOTPRODUCT | 2 | all pure arithmetic; gate each behind its own header, added as programs need them |
| MATH FFT | 2 | pure but big; on-demand header; cc-code speed noted |
| MATH PID, MATH SENSORFUSION | 5 | only meaningful with periodic ticks — inherits SETTICK's category |
| MANDELBROT | 4 | demo command; write it in BASIC |
| BIT(/BYTE(/FLAG( command forms | 1 | inline, trivial |
| FM fixed point | 4 | not in the reference variant |

### Files

| feature | cat | rationale |
|---|---|---|
| FLUSH | 1 | one fflush wrapper |
| wildcard/bulk KILL ALL, COPY, FILES sort options | 3 | spawn rm/cp/ls; the wildcard matcher already exists for DIR$ |
| LOAD BMP/JPG/PNG | 3 | extend loadimage (decoders already vendored in the wider tree) |
| SAVE COMPRESSED IMAGE | 3 | extend saveimage |
| VAR SAVE/RESTORE/CLEAR, SAVE/LOAD DATA, SAVE PERSISTENT | 2 | file-backed; cheap now the file layer exists |
| DRIVE, SAVE/LOAD CONTEXT | 4 | one filesystem; contexts are an interpreter notion |
| serial OPEN "COMn:" | 2† | Fuzix tty devices; needs termios via the libcall surface; correctly refused today |

### Graphics

| feature | cat | rationale |
|---|---|---|
| BOX, RBOX | 2 | rectangles → existing mm_fill batches; the most-missed primitive and near-free |
| ARC, TRIANGLE, POLYGON, BEZIER, LINE AA/PLOT/GRAPH | 2 | the CIRCLE bargain: geometry in a header, only mm_plot/mm_fill cross |
| FILL (flood) | 2 | needs pixel readback; mm_pixel_get exists |
| BLIT + BLIT MEMORY | 2† | buffers in PSRAM heap arrays; needs a block pixel-read ioctl to pair with RECTS |
| GETSCANLINE | 4 | FRAMEBUFFER WAIT covers the use case |
| FRAMEBUFFER LAYER/MERGE, second buffer | 5 | a driver-side layer needs ~40K of SRAM the machine does not have |
| SPRITE (28 verbs), TILEMAP, TILE | 5 | background-save state, collision, interrupts belong to an interpreter idle loop; the honest subset is BLIT |
| TURTLE | 5 | pure geometry but ~35 verbs of stateful toolkit; tenuous |
| DRAW3D, RAY | 5 | the canonical category-5 example |
| DEFINEFONT | 5† | kernel owns the text engine; per-program fonts need kernel font-upload surface |
| RESOLUTION, SYNC, REFRESH | 4 | kernel owns video timing; MODE covers legitimate use |
| COLOUR MAP | 2 | palette arithmetic over existing mm_map |

### GUI / input

| feature | cat | rationale |
|---|---|---|
| GUI controls (~35 verbs), CTRLVAL, MSGBOX | 5 | full widget toolkit; pcgui is the PC3-native answer |
| TOUCH(), CLICK() | 4 | no touch hardware |
| KEYDOWN() | 2† | needs a kernel key-state ioctl; wrapper is tiny |
| MOUSE, DEVICE(MOUSE…) | 5† | needs a kernel USB-mouse driver and reopens the pointer/layer question |
| GAMEPAD, WII, KEYBOARD ON/OFF | 4 | wrong variant / no hardware path |
| FRAME text windows | 4 | Fuzix ttys are the windowing story |

### Sound

| feature | cat | rationale |
|---|---|---|
| PLAY WAV/FLAC/MODFILE | 3 | clone the playmp3 pattern; dr_wav/dr_flac/hxcmod already vendored nearby |
| PLAY TONE | 3 | small tone-generator program |
| PLAY MIDI/MIDIFILE, SOUND/NOTE synthesis | 5 | a real synthesiser to write; tenuous |
| PLAY PAUSE/RESUME | 3 | SIGSTOP/SIGCONT to the kernel-reported owner — the mm_play_stop model |
| PLAY NEXT/PREVIOUS, LOAD SOUND/SAMPLE/ARRAY/STREAM/EFFECT | 5 | playlist/streaming state belongs in a player; idle-loop refill forms cannot be honest |

### Hardware I/O

| feature | cat | rationale |
|---|---|---|
| SETPIN AIN/ARAW, ADC | 2† | kernel ADC ioctl + header wrapper; genuinely useful here |
| PWM, SERVO, SETPIN PWM… | 2† | kernel PWM ioctl + header wrapper |
| SETPIN INTH/INTL/INT/FIN/CIN/PIN | 5 | pin interrupts into a user process need a signal design; polling PIN() covers most uses |
| I2C/I2C2 incl. slave | 2† | /dev/i2c with kernel arbitration is the agreed model |
| SPI/SPI2 | 4 | the SPI bus belongs to the SD card |
| ONEWIRE, TEMPR, HUMID | 5† | µs bit-timing under a preemptive kernel needs a kernel driver |
| PIO + 23 assembler mnemonics | 4 | PIO blocks are owned by video/audio scanout |
| IR, WS2812, BITSTREAM, PULSE, PULSIN(), DISTANCE(), KEYPAD, STEPPER/TMC22xx/SLEW, CAMERA, GPS(), LOCATION, STAR/ASTRO | 4 | timing-critical bit-bang or hardware the PC3 does not expose |
| RTC GETTIME/SETTIME/GETREG/SETREG | 4 | the OS owns the DS3231; DATE$/TIME$ are the interface |
| WATCHDOG, CPU SPEED/RESTART/SLEEP, UPDATE FIRMWARE | 4 | kernel/OS responsibilities (QMI flash timing must span clock changes) |

### Memory, misc, MM.*

| feature | cat | rationale |
|---|---|---|
| PEEK/POKE raw addresses, PEEK(PROGMEM/VARTBL/…) | 4 | no MMU — a wild pointer takes the machine down; the refusal is a safety feature |
| PEEK(VAR/VARADDR), POKE VAR | 1 | translator-resolvable to real C addresses; safe subset |
| MEMORY COPY/SET/PACK/UNPACK (typed, on variables) | 2 | pure memmove arithmetic |
| MEMORY report, RAM, FLASH families | 4 | OS owns storage |
| JSON$() | 2 | WebMite-only in firmware but pure parsing; optional on-demand header (cJSON) |
| MM.VER, MM.DEVICE$, MM.CMDLINE$ (argv), MM.HPOS/VPOS†, MM.FONTWIDTH/FONTHEIGHT, MM.INFO(FILESIZE/EXISTS/VERSION/DEVICE) | 1 | trivial and small; MM.ERRNO/ERRMSG$ ride with ON ERROR |
| remaining ~90 MM.INFO selectors | 4 | interpreter introspection with no referent here |
| WEB family, MM.MESSAGE$/ADDRESS$/TOPIC$ | 4 | no radio; wrong variant |

Score after allocation of the ~350 applicable keywords: ~120 covered,
~35 more to category 1, ~60 to category 2, ~15 to category 3,
~90 category 4, ~30 category 5.  Only TYPE touches the compiler
(tokenizer); everything else is translator + runtime + headers + spawned
binaries, plus the small kernel ioctls marked †.

---

# The plan

**Status at the end of the 2026-08-08 session: Sections 1, 2, 3 and 4
are CLOSED** — both translators (cgate 0 throughout), all host gates
green (make check 22 ok, xcheck 4896/0, fcctests 22/22, qemutests
23/23), board-verified self-hosted on the PC2, and every section signed
off side-by-side against a real MMBasic machine.  The kernel is at
release **0.10** and flashed.

The side-by-side has now found a divergence in three sections running,
each of them invisible to every gate and to the board because the
implementation was self-consistent and wrong (see
[[pc3-side-by-side-authority]] in the memory):

| section | what it caught |
|---|---|
| 3 | over-length member strings must RAISE, not truncate |
| 4 | `PRINT` is all or nothing - a statement that fails part way prints nothing |
| 4 | entering a SUB costs skip count, so `SKIP 2` protects less than it looks |

**On the card now** (each with a `.prev` rollback): bcrun 85,900, mmbc
93,748, mmedit 33,840, current cc0/cc1/cc2, all mmb headers, the
updated `/usr/lib/cc/include/mmb_runtime.h`, and the test set
box/tri/type/structtest/checks/onerror/solar + mmv.  Both manuals and
their PDFs are current; the PicoMite structures-manual errata are fixed
upstream in d:\Dropbox\PicoMite\PicoMite\docs (uncommitted - the user
manages that repo).

**Performance, measured this session on the board** (solar eclipse,
self-hosted): 2.3527s baseline → **2.2511s** after the literal-divisor
fix, which is at or ahead of the v0.9 figure of 2.27s.  The regression
was Phase A's own doing and is written up under Section 4.

Two incidental fixes: mmedit's prompts ignored Enter from the USB
keyboard (a serial terminal sends CR, the keyboard map sends LF, and
`GetInputString` took only CR - so `F3` Find looked broken while every
other key worked), and `PICOIOC_BOARD` was added to the kernel because
which board this is was something only the boot banner could say.

NEXT: Section 5 (PLAY WAV/FLAC/MODFILE and PAUSE/RESUME), or Section 6
(the 1→2 migration) if a smaller bcrun matters more than more audio.

Process: one section at a time.  A section is not done until (a) the
host gates pass — `make run`, `make xcheck`, `mmbc/cgate.sh` at total 0,
`fcc/fccbuild.sh` clean on the affected tests — and (b) the change is
confirmed on the physical PC3: the affected programs compiled on-board or
copied on, run on hardware, output/pixels checked, sizes recorded.  Both
translators (mmb2c.py and mmbc) move in lockstep within each section;
cgate.sh is the authority that they agree.

## Section 1 — split mmb_gfx.h per feature  ← CURRENT

The one-flag include means a TEXT-only program carries the whole circle
machinery (self-recursion defeats the name-count DCE) and ~1.9K of
extent-table BSS.  Split by feature so each include carries exactly one
primitive family:

* `mmb_gfx_pts.h` — MMG_BATCH, `mmg_pt`, `mmg_rc` (the batch helpers,
  shared by every span/point primitive present and future).
* `mmb_gfx_circle.h` — MMG_RMAX, `mmg_extent`, the extent tables,
  `mmg_ring`, `mmg_circle`.  Includes `mmb_gfx_pts.h`.
* `mmb_gfx_text.h` — the justification defines, `mmg_upper`, `mmg_just`,
  `mmg_text`.
* `mmb_gfx_map.h` — `mmg_map_maximite`, `mmg_map_greyscale`.
* `mmb_gfx.h` stays as a thin umbrella including all of the above, so
  hand-written C on the board and old generated C keep compiling.

Translator: `uses_gfx` becomes `uses_circle` / `uses_text` /
`uses_mappal`, one per header, emitted in that fixed order.  Mirrored in
mmbc (`mmbc.h`, `mmbc_stmt.c`, `mmbc_out.c`).  Distribution:
`fcc/sync-runtime.sh`, `mkccimage.sh` and `verifyimage.sh` in FUZIX
Applications/CC carry the new file names; the board's copy lands in
`/usr/lib/cc/include`.

Baselines (host fccbuild, 2026-08-07, before the split):
`text.bc` 25,099 bytes · `circle.bc` 14,277 · `palette.bc` 4,498.

Gates: host suite + cgate 0; `text.bc` shrinks (circle code and extent
BSS gone); `circle.bc` and `palette.bc` essentially unchanged; pixel
output of circle/text/palette tests identical before and after.
Board: rebuild the graphics tests on-board with the new headers in
/usr/lib/cc/include, run on hardware, confirm drawing unchanged and the
size drop is real.

**Results (2026-08-07).**  Host: `make check` clean, xcheck 4896/0,
samples build, cgate 0, fcctests 16/16, qemutests 17/17.  Host sizes:
text 25,099 → 14,464 (−42%), circle 14,277 → 12,776 (−1.5K: the
dead-but-named justification helpers no longer survive), palette
unchanged.  Board (on-board mmbc + cc): circle 9,897 → 8,912,
fbtext 13,209 → 12,224 (both −985, the text machinery), palette
unchanged; text.bas compiles on-board at 13,020 carrying only the
text and map headers.

**Board crash found during verification — not a split regression.**
Running `text.bc` (the first program ever to call `mm_fontinfo` on
hardware) panicked the machine: EXCEPTION 3, CFSR=0x01000000 =
UsageFault UNALIGNED escalated to HardFault, at the `strd` in
`mm_fontinfo`'s `*w = 0`.  Root cause `bcrun.c`: the demand-sized
`mem[]` (MM_PC3 only, on the board since the morning of 2026-08-07)
malloc'd an UNROUNDED `need`, and `run()` sets `sp = MEMTOP - 4` — so
any program whose data+bss ≢ 0 (mod 4) runs with every VM frame
misaligned.  The M33 tolerates unaligned words, so everything works
silently until native runtime code does an STRD through a by-ref frame
local — which only the TEXT path does today.  qemu/x86 never see it
(static aligned `mem[MEMSIZE]`).  Diagnosis chain: addr2line + objdump
on the bcrun ELF (fault pc = the `strd`, pointer in r5), bcdump of the
object (caller's slots provably correct), CFSR decode (UNALIGNED, and
BFAR/MMFAR invalid — they read as their own addresses), LAZYBIND A/B
(exonerated R4 at the cost of one fsck).  Fix: round `need` to 8 before
the malloc (one line).  Fixed bcrun passes fcctests 16/16 and
qemutests 17/17.  The fault-dump in `platform-rpipico/main.c` now
prints r4–r7 before r0–r3, because the console TX died mid-dump twice
and r5 was the line that mattered (takes effect on the next kernel
flash).

**Board-verified 2026-08-07 afternoon.**  The fixed bcrun (79,308,
`/usr/bin/bcrun.prev` kept as rollback) went on via TeraTerm send-file
at 20 ms/line after three pyserial full-rate transfers wedged the
console (same `ri=0470` OE signature each time; the echo path — every
received line echoed to uart + screen, screen scroll in masked context
— is the prime suspect for the rx overrun; `stty -echo` during
transfers and a per-line gap are the interim discipline).  `text2.bc`
then ran clean through every TEXT statement — the formerly fatal strd
path — and stopped at the honest `Error: MAP needs a 16-colour mode`
(text.bas is a host-gate program and never issues MODE 2; on hardware
MAP correctly refuses outside a 16-colour mode).  circle.bc and
palette.bc completed silently, fbtext.bc animated 600 frames in
3030 ms vs 3097 ms on the pre-split build.  Section 1 is closed:
per-feature headers live on board and host, both translators, all
gates, sizes and behaviour confirmed on hardware.

## Section 2 — BOX, RBOX, TRIANGLE, ARC

New per-feature headers in the Section-1 pattern, geometry taken from
Draw.c so pixels match the interpreter: `mmb_gfx_box.h` (edge + fill
rectangles), `mmb_gfx_rbox.h` (Bresenham corner arcs, the firmware's
own recursion for the filled outline), `mmb_gfx_triangle.h`
(CalcLineInternal scanline fill + mm_line outline; SAVE/RESTORE
refused), `mmb_gfx_arc.h` (ring-sector scanline sweep, compass
angles).  Shared normalising rect helpers joined `mmb_gfx_pts.h` —
DrawRBox hands DrawRectangle right-to-left spans, and the batched
crossing does not normalise.

**Implemented and verified 2026-08-07 (same day, after Section 1
closed).**  All four primitives in both translators, `tests/box.bas`
and `tests/tri.bas` with .expected files in all three gates: make
check 18 ok, cgate 0, fcctests 18/18, qemutests 19/19.  box.bc 11,796
via fcc carrying exactly `mmb_gfx_box.h` + `mmb_gfx_rbox.h`; 8,552
compiled self-hosted on the board.  Verified on hardware AND
side-by-side: the user ran the same two .bas files on a real MMBasic
machine and both screens matched — "both perfect" — including the
negative-size box, the zero-height guard, the bare-comma defaults,
the radius-0 RBOX, the collinear triangle and the arc across north.
Section 2 is closed.

## Section 3 — TYPE / structures

`TYPE…END TYPE` → C struct.  The tokenizer fear turned out smaller
than COVERAGE.md predicted: a dotted name is already ONE token, so the
firmware's own rule (split at the first dot at lookup time, prefix
wins if it names a struct variable) needed no lexer change at all —
only `.` as an operator for the `arr(i).member` case.  Both
translators change in lockstep; the type tests join the suite.

**Python side DONE 2026-08-07 (d6ebede).**  Semantics distilled from
the firmware into `TYPE-SPEC.md` (a subagent swept PrepareProgramExt,
ParseStructMember, ResolveStructMember, cmd_struct and fun_struct —
and found seven errata in the official structures manual, which were
fixed and its PDF rebuilt the same day).  The firmware's byte layout
is reproduced exactly, explicit pads and all; member strings are
Pascal fields with no trailing NUL, hence the new bounded runtime
setter `mm_ssetm` (reads go through `mm_scopy`).  Working: member
access to full nesting depth incl. member arrays and
`data(2).items(1).values(4)`, whole-struct assignment (safe cases),
struct parameters by reference, LOCAL structs (per-invocation block,
recursion correct), scalar initialisers, STRUCT COPY/CLEAR/SWAP, and
compile-time STRUCT(SIZEOF/OFFSET/TYPE).  Refused with messages:
SORT/SAVE/LOAD/PRINT/EXTRACT/INSERT/FIND, struct-returning functions,
struct-array parameters and initialisers, and the two firmware
defects (whole-struct assignment into/out of a nested member overruns
in the interpreter).  The firmware's own Testfiles/StructTest.bas:
58 tests PASS on the host, every FAIL a designed refusal, zero
unexplained.

**mmbc mirror + board verification DONE, same day.**  A subagent
ported the whole feature into the C translator to byte-identity
(cgate 0 with the struct tests INSIDE the suite, the firmware's seven
struct Testfiles diff 0 in both modes); tests/type.bas (the layout
constants: Seg 56, all-string Tags 31) and tests/structtest.bas (the
firmware's own suite, verbatim) joined all three gates — 20 host ok,
fcctests 20/20, qemutests 21/21.  Two board-only potholes on the way:
cc1's NUM_STRUCT_FIELD (50) was smaller than StructTest's mm_vars, so
it is 128 now; and the Fuzix libc has no %lld, so the ARM mmbc
silently dropped a printf argument the host build printed — caught
because type.bas would not compile on the card, fixed by %ld casts
(the values are all small).  Final state on hardware, fully
self-hosted (board mmbc → board cc → board bcrun): type.bas prints
the identical fifteen lines including 16/56/31/120, and
StructTest.bas gives the identical 81-line refusal report and the
same 58 PASS / 45 FAIL.  Side-by-side on a real MMBasic machine
pending — run tests/type.bas and tests/structtest.bas there; type's
"sizes:" line must read 16 56 31 120, and every test our port PASSes
must PASS there with the same values.

## Section 4 — ON ERROR SKIP/IGNORE, MM.ERRNO, MM.ERRMSG$

Soft error handling: a checked error flag (or setjmp) through the
runtime, `mm_error` records instead of exits when armed.  Touches many
runtime calls, so it gets its own section and a dedicated test that
exercises every failure path the manual documents.  MM.VER, MM.DEVICE$,
MM.CMDLINE$ and the trivial MM.INFO selectors ride along.

### Mechanism: why not setjmp/longjmp

The firmware's own model is `longjmp(ErrNext, 1)` out of `error()`
(MMBasic.c:5632) into a `setjmp` that `ExecuteProgram` takes per
statement (MMBasic.c:1852-1868).  That is not available to us: bcrun
has no setjmp at all, and a jump would have to cross two worlds —
`call_target` dispatches bytecode calls iteratively (push pc, set pc)
but native functions through `native_enter`, a real ARM call on the
host C stack.  With most functions going native, an error five BASIC
calls deep sits under five real ARM frames.  Resuming would mean
implementing setjmp/longjmp in the ARM backend and the VM, in both
execution modes — a large hardware-only risk of exactly the kind that
produced the STRD alignment fault.

Instead: a **poison flag**.  `mm_error` sets MM.ERRNO/MM.ERRMSG$, sets
the flag and returns when armed; the flag means "unwinding to the end
of the current BASIC statement" and is honoured in three places, all
emitted only when the program contains ON ERROR — a statement epilogue
(the resume point, the skip decrement, the flag clear), a commit guard
(the store happens only if the statement survived) and a function-entry
guard (a callee entered with poison set returns at once, which is
observably identical to MMBasic never entering it, and burns no skip
count).  `goto`, not `break`: `EXIT DO` emits a real `break` that a
`do{...}while(0)` wrapper would swallow.  The flag lives in the
PROGRAM's memory (`mm_err_bind(&__mm_err)` at startup, runtime writes
through the translated pointer as it already does for by-reference
parameters), so a guard is a load and a branch, not a libcall.

Block headers reproduce the firmware's quirk exactly, because the
translator knows the form: MMBasic resumes at the textually next
statement, so an erroring condition in a MULTI-LINE `IF` falls into the
THEN body while a single-line `IF ... THEN stmt` skips the lot —
`(__mm_err ? 1 : cond)` against `(!__mm_err && cond)`.  Same for
`WHILE`/`DO WHILE`, which fall into the body.

Nothing unwinds, so — unlike the firmware, which has to
`ClearTempMemory`/`ClearVars` on its jump leg — no scratch stack or
LOCAL block leaks.  `ON ERROR RESTART` (soft-resets the machine) is
refused with an mmbc error: a compiled program has no equivalent.
Ctrl-C stays outside all of this, as it is in the firmware, where abort
goes through `CheckAbort()` and not `error()`.

### Phase A — the checks have to fire before they can be skipped

An error that is never raised cannot be skipped, so the section opens
with a sweep of every check the firmware makes and we do not
(`error()` in Operators.c/Functions.c/MATHS.c against `mm_error()` in
mmb_runtime.c, resolving `StandardError(n)` through `errorstring[]`).
It found real divergences that predate this section:

* **Float `/` was a bare C divide** — `PRINT 1/0` printed `inf` where a
  PicoMite errors.  No hardware trap would have caught it either: IEEE
  returns inf, and the M33 does not trap integer divide-by-zero unless
  `CCR.DIV_0_TRP` is set.  Now `mm_fdiv`, which tests first exactly as
  `op_div` does.
* **`mm_scat` truncated at 255** where `op_add` raises "String too
  long" — the same mistake `mm_ssetm` made in Section 3, which the
  board caught.
* **SQR/LOG/ASIN/ACOS emitted raw C library calls**, so `SQR(-1)` and
  `LOG(0)` returned nan/-inf instead of "Negative argument" and
  "Divide by zero".  ASIN/ACOS also special-case the endpoints in the
  firmware so the answers there are exact.
* **`ASC("")` errored here and returns 0 there** — a legal PicoMite
  program died on this one.
* **Message texts**: ours said "Division by zero" where the firmware
  says "Divide by zero"; BIN2STR$/STR2BIN had invented wordings where
  the firmware uses "Overflow" and "String length".  Invisible while an
  error only ended the program — MM.ERRMSG$ makes the text a value a
  program can read.

**The `Overflow` checks were tried and REJECTED, 2026-08-08.**  The
firmware checks `fret == INFINITY` after float `+ * ^ /` (note `+inf`
only, so `-1e308*10` does NOT error there).  Matching it means routing
every float add and multiply through the runtime, and on the board that
is a call across the VM boundary where there had been an inline FPU
instruction.  Measured: the eclipse benchmark's object grew **31%**
(135,942 → 177,888 bytes) before it even ran, and then it failed
outright — which looks like a bug in the attempt (most likely the
operand slots in the new wrappers) rather than anything about the
firmware's semantics, but the size alone settled it.  Reverted whole.
A program that overflows a double gets `inf` here and an error there;
that is a documented gap, not a silent one, and it is cheap to revisit
if a real program ever wants it.
Type checks that a typed translator makes at compile time (BIT "Not an
integer", BYTE "Not a string", MID$/MATH "Argument count") do not
apply.  MATH's array-shape errors belong with the MATH backlog in
Section 7.

`tests/checks.bas` covers the non-erroring side of every fix (exact
values only) and is a side-by-side program: it must print the same
lines on a real PicoMite.

**Phase A done 2026-08-07** (mmb2c `51e15e5`, FUZIX `44012bb0e`): all of
the above in both translators, cgate 0, make check 21 ok, xcheck 4896/0,
fcctests 21/21, qemutests 22/22.

**And it cost 3.7%, which took a day to notice.**  Making float division
a runtime call put a VM-boundary crossing where an inline divide had
been, and the eclipse benchmark went 2.27s → 2.3527s.  The fix is that
a literal divisor which is not zero cannot be zero: 42 of that
program's 77 division sites divide by 180, 86400, 36525, 648000,
298.257 or pi, and there the interpreter's own test could never fire
either, so those emit a bare divide again.  A literal ZERO still goes
through `mm_fdiv` and still errors.  Board: **2.2511s**, ahead of where
v0.9 was, and the object is smaller too.

The lesson is not "don't add the check" — the check is correct and the
interpreter has it.  It is that a correctness fix on a hot path needs a
measurement in the same sitting, because nothing in the gates will ever
mention it.

### Phase B — the machinery

Done the same day.  `mm_error` records and returns while armed; the
three guards go in only when the program contains ON ERROR, and a
program that does not is **byte-identical to what it was before** (t7,
solar_eclipse, circle, box and structtest all diff clean against the
previous translator), so nothing already measured moves.

The part that was not obvious from the design: **every raise site in the
runtime had been written assuming `mm_error` never comes back**.  87 of
them, and the ones that mattered were not the arithmetic checks but the
ones that would carry on using the value they had just rejected —
`mm_byte` indexing a string with the index it refused, `mm_open` seeking
the NULL it had failed to open, `mm_tmp` handing out a pool slot past
the end, `mm_ch` returning a channel that is not open to `fputc`.  They
all leave through `MM_RAISE`/`MM_RAISEV` now, and `mm_ch`/`mm_ls_file`
return NULL with every caller checking.  Out of memory is the one thing
that stays fatal whatever ON ERROR says (`mm_fatal`): there is no
benign array base to return, and a NULL one would fault somewhere else
entirely.

Side effects are stopped at the funnels — `mm_putc`, `mm_ssetn`,
`mm_ssetm`, `mm_outc` — so a PRINT that fails half way prints what it
had printed and no more, exactly as the interpreter's jump does.

`tests/onerror.bas` is in all five gates and is the side-by-side
program for this section.  It covers: the assignment whose expression
failed leaving the variable alone, SKIP lasting exactly one statement,
SKIP n covering n, IGNORE persisting until CLEAR, MM.ERRNO/MM.ERRMSG$
values, the rest of a failed PRINT being skipped, an error inside a SUB
resuming at the SUB's next statement while the caller carries on, and a
FUNCTION whose result expression failed.

Known and documented divergences: a statement that jumps away (GOTO,
EXIT) does not decrement the skip count.  It only matters for SKIP n
with n > 1 spanning one.

**Side-by-side on a real PicoMite, 2026-08-08 - it found two things
nothing else could.**  checks.bas matched outright.  onerror.bas
disagreed twice, and the interpreter was right both times:

* **PRINT is all or nothing.**  `PRINT "part: "; 1/0; " never"` printed
  nothing there and `part: ` here.  cmd_print builds the whole line into
  one buffer and writes it at the end (Commands.c:1060), so the error
  takes the buffer with it.  Printing item by item, as we did, left
  everything before the failure on the screen.  Now, while armed, output
  goes to a line buffer that the statement guard commits or discards -
  only while armed, so a program without ON ERROR still writes straight
  through and is byte-identical to before.
* **Entering a SUB costs skip count.**  `ON ERROR SKIP 2` then a call
  whose third statement fails: the interpreter stopped, we skipped it.
  The SUB line itself is a statement it executes and counts, and we
  counted only the LOCAL - so our count reached one statement further
  into the callee than the interpreter's.  A guard at routine entry
  fixes it, and the program now stops exactly where the PicoMite does.

Both were invisible to every host gate and to the board: the code was
self-consistent and wrong.  tests/onerror.bas now ends deliberately with
the count-exhaustion case, so the abort and its message are part of the
expected output (tests/onerror.rc = 1).  The fcc gate then caught a
missing mm_pr_commit wrapper in bcrun before the hardware did.

## Section 5 — PLAY WAV / FLAC / MODFILE, PLAY PAUSE / RESUME

Clone the playmp3 pattern: one cross-compiled player per format
(dr_wav, dr_flac, hxcmod), FPU-licensed, kernel arbitrates the device.
PAUSE/RESUME = SIGSTOP/SIGCONT to the kernel-reported owner, following
mm_play_stop.  Board verification is the only verification that counts
here (audio).

## Section 6 — move cold pure-arithmetic runtime out of bcrun

The 1→2 migration from Part 1: LONGSTRING, FORMAT$, BIN2STR$/STR2BIN,
date/time civil arithmetic, the INKEY$ escape decoder, BYTE/TRIM$/FIELD$,
then file management via the libcall surface.  SORT and the MATH()
reductions stay native for speed.  Each move: header + translator flag +
mmwtab entry removal + bcrun rebuild + board size measurement.  This
section shrinks bcrun's floor for every process and is worth a fresh
measurement pass against REVIEW-2026-08-06's numbers when it lands.

## Section 7 — allocation backlog (on demand)

Category-2 maths (MATH matrix/vector/CRC/FFT), MEMORY COPY/SET,
VAR SAVE, JSON$, ARRAY SLICE/INSERT, REDIM, CALL(name$), RUN/CHAIN,
FLUSH, wildcard file ops, LOAD BMP/PNG/JPG.  Pull items forward when a
real program wants them; each follows the pattern its category dictates.

### MATH: where the four we have live, and where the other 34 must not

Measured 2026-08-08 on the armm0 bcrun.  `MATH` as a STATEMENT is 38
sub-commands in the firmware and 4 here — `SET`, `ADD`, `SCALE`,
`RANDOMIZE`, the ones that walk an array element by element, with
`ARRAY` accepted as a spelling.  They are **category 1**: `mm_arr_set_*`,
`mm_arr_add_*`, `mm_arr_scale_*` and `mm_randomize` in mmb_runtime.c
with their `w_arr_*` wrappers, all inside bcrun.

    the four, with wrappers      876 bytes
    bcrun .text                84,249 bytes

so about 1% of bcrun, and **every process pays it** whether it says
MATH or not — a running .bc is a bcrun process against the 340K pool,
so bcrun's text is subtracted from what every program has to work in.

For these four that is the right trade: they are small, and a loop
setting or scaling a whole array wants to be native rather than
something the bytecode walks.

**For the other 34 it would be badly wrong.** The firmware's MATHS.c is
4,562 lines: matrix inverse, FFT, quaternions, sensor fusion, AES.  That
is a great deal of code that very few programs touch, and in bcrun it
would shrink the pool for every process on the machine to benefit almost
none of them.  They are category 2 — a header of static functions, where
cc1's dead-code elimination means a program that never names
`MATH M_INVERSE` carries none of it.

**One header per block**, not one `mmb_math.h`: `mmb_math_matrix.h`,
`_vector.h`, `_quat.h`, `_complex.h`, `_fft.h`.  The dead-static rule
counts NAMES rather than reachability, so anything a header defines that
a table or a recursion keeps alive survives into every program that
includes it — the include is the granularity.  That is the lesson
Section 1 paid for with the circle extent tables, and a maths header is
exactly the shape to repeat it with.

Whether the existing four should move out to match is marginal and the
answer is probably no: it would take 876 bytes off every process and add
about the same to each program that uses them.  Leave them, and start
the header pattern with the new blocks.

---

*Reports corrected as part of this review (2026-08-07): COVERAGE.md no
longer claims BOX/BLIT; README.md's "Currently translated" and "Not yet"
sections reflect INKEY$, graphics, GPIO, PLAY, the spawns and the heap
split.*

# Running the Game*Mite games on the PC3

Reviewed 2026-08-16 against the eighteen programs in the Game*Mite
distribution (2026-08-14 snapshot). The question was: with the menu
system stripped out, what does mmb2c still need?

## Method, and why the first answer was wrong

Every program was put through the real translator with `--report`, then
the C it produced was compiled with the host compiler. Nothing here is
read off a keyword table.

The first pass said eight of the games translated with **zero**
problems. That was wrong, and wrong in the flattering direction: a
fatal decl-pass error stops the statement passes, so those eight had
simply failed on line one of their first `DIM` and never been parsed at
all. The two blockers (run-time array bounds, `DefineFont` into a
built-in slot) were lifted in a throwaway copy of `mmb2c.py` —
`/tmp/gamemite/full3.sh`, a survey instrument, not a change to the
translator — and everything was translated again. Every number below
comes from that second run.

After it, sixteen of the eighteen translated — GNR_6 and Pico-Blocks
are blocked by their own source, see "Two defects" below — and **nine
of those sixteen produce C that compiles clean.**

## The two clusters

The games divide much more sharply than the raw error counts suggest.

**Cluster A — a handful of small gaps each.** 2048, ChemiChaos8,
Pico-Man, stoned, Pico-Blocks, GNR_6, Snake, Breakout, Flappy Bird,
Pico-Frog, shootinggameg_bf. Each of these needs `MM.INFO()`
sub-keywords, `GPn` pin names, and its menu call removed. A few need
one thing more apiece.

**Cluster B — one shared library, six games.** 3D-maze, Circle,
FileManager, Kingdom, Lazer-Cycle and Pico-Vaders all embed Thomas
Hugo Williams' `ctrl` / `twm` / `sound` includes. They fail on the same
five things because it is the same code six times. Support that one
library and six games move at once — and the library is what a
*seventh* Game\*Mite program would most likely be built on too.

Nothing else in the set is like either cluster.

## Input: the actual constraint

The games read eight switches on **GP8–GP15**. On the PC3 those pins
are not available at all — `pinlock.c` allows only the I/O header
(GP0–GP7, GP26, GP34–GP46) plus GP32, and everything else belongs to
the display, the SD card, the PSRAM, the sound hardware or the console.
So GP8–GP15 is not a matter of preference; the claim is refused.

**GP0–GP7 is exactly eight pins** and is the obvious remap, at the cost
of GP0/GP1, which are also the second serial port `/dev/tty2`.
**GP34–GP41** is eight pins too and costs nothing, so it is the better
default; the port should not hard-code either.

**The keyboard is the better answer anyway, and it is nearly free.**
MMBasic's `KEYDOWN()` reports up to six *simultaneously held* keys —
which is what a game needs and what `INKEY$` structurally cannot give:
diagonal movement, or firing while moving. And the work is already
done: `kbd_decode.c:311` is `usb_kbd_keydown(n)`, carrying MMBasic's
`fun_keydown` semantics exactly (n=0 count, 1–6 the held codes, 7
modifiers, 8 locks). It is not reachable from userland and mmb2c has no
token for it. That is the whole gap.

Note also that Cluster B already ships a keyboard driver:
`ctrl.init_keys` fills a 256-byte key map from `On Key`, and
`keys_cursor` reads the arrows out of it. Those six games get keyboard
play with **no game edits at all** once the library translates — the
Game\*Mite driver is only one of several the framework can be pointed
at. And `shootinggameg_bf` already writes `Pin(GP8)=0 Or KeyDown(1)=129`,
so the intended shape is in the corpus already.

## What to build, ordered by games unlocked

| # | feature | games | note |
|---|---|---|---|
| 1 | `MM.INFO()` sub-keywords | 14 | `Option Base`, `PinNo`, `FontHeight`, `FontWidth`, `Exists File/Dir`, `Path`, `Drive`, `Platform`, `Version`, `Device`, `FileSize`. Mostly table lookups. `MM.INFO$()` too. |
| 2 | `GPn` as a pin constant | 9 | On the PC3 a pin *is* its GPIO number, so `GP8` = 8. Today `Pin(GP8)` silently becomes `Pin(0)` where `OPTION EXPLICIT` is off — a wrong pin, not an error. |
| 3 | `CALL name$, args` | 6 | Indirect dispatch. Every candidate SUB is known at translate time, so this is a name→function-pointer table, not an interpreter. **Cluster B.** |
| 4 | `PEEK/POKE VAR`, `VARADDR` | 6 | Byte access into a variable or array. **Cluster B.** |
| 5 | run-time `DIM a(n)` / `LOCAL a(n)` | 5 | Same machinery as `REDIM`, already on the coverage plan. |
| 6 | `MEMORY SET` / `MEMORY COPY` | 5 | memset/memcpy over a machine address. **Cluster B.** |
| 7 | `KEYDOWN()` | — | Kernel side exists. See above. |
| 8 | `LINE` with a width | 4 | Currently a warning that draws 1 pixel. |
| 9 | `EXECUTE` | 4 | Only ever used two ways here: `On Key <n>, <name>()` and `SetTick <n>, <name>, <slot>`. Both are the *indirect-call* problem again, not general `EVAL` — worth solving as a special case rather than declining. |
| 10 | `DefineFont` into slots 1–9 | 3 | Circle, GNR_6, Pico-Frog replace a built-in. Refusing is right for the shared console fonts; a per-program override table is not. |
| 11 | `BLIT LOAD` | 1 | Snake. A BMP region straight into a blit buffer. |
| 12 | `TILEMAP` | 1 | Breakout. A whole subsystem for one game. |
| 13 | `GUI BITMAP` | 1 | Pico-Vaders. One call, easily rewritten in the port. |
| 14 | `CSUB` | 1 | Flappy Bird has 273 lines of Thumb hex. Not ours — a compiler links objects. GNR_6's is small enough to rewrite in BASIC. |

`RUN`, `DRIVE`, `FLASH RUN` and the `MM.INFO(Platform)="Game*Mite"`
tests appear in thirteen games and are all menu plumbing: they go out
with the menu, not into the translator.

## Two defects found on the way

**`CONST` inside a SUB is scoped globally; MMBasic scopes it to the
routine.** `do_const` writes into `self.globals` unconditionally
(mmb2c.py:3187). The reference is explicit — `cmd_const` sets
`if (g_LocalIndex != 0) type |= V_LOCAL;` (Commands.c:6478). The
symptom is 22 lines of `'f' is STRING but used as INTEGER` in 3D-maze,
where `Const f$` in one SUB poisons `For f%` in another. Reproduced in
eighteen lines in `/tmp/gamemite/constscope.bas`. This is a
correctness divergence, not a gap, and should be fixed whatever happens
to the games.

**MMBasic tolerates structurally broken code that never runs; a
compiler cannot.** Pico-Blocks has a `For y = ...` with no `Next`
(line 306 — the loop is really the `Do While` under it), and GNR_6 has
a `Sub addgem` closed with `Exit Sub` and no `End Sub` (line 203).
MMBasic parses at run time and never notices. Both are one-line fixes
in the game source; the point is that this class exists and will
recur, so the porting notes should name it.

## STATE AS OF 2026-08-17 — read this first

**No game in the corpus stops on a fatal error any more** (it was nine).

Shipping in `/root/MMBasic` via `mkexamples.sh`: **Pico-Vaders**,
**PicoMan**, and **picofrog** — which was already there and *is*
Pico-Frog: same author, ported from his upstream `picofrog6_b9.bas`,
where the Game\*Mite `Pico-Frog.bas` is a variant of it with a Wii
nunchuck bolted on. Re-porting that variant would buy only the
nunchuck. **Do not treat it as an outstanding game.**

**`DefineFont` into slots 1–9 is NOT a gap.** The convention, stated in
picofrog's own header, is to renumber the program's font to 10–16:
*"Fonts 1–9 here are the built-in nine, shared with the shell; 10–16
are a program's own."* One line per program. Refusing 1–9 is correct.

Two games remain, and between them they need three things:

| game | needs |
|---|---|
| **Circle** | `CALL name$` (the function-pointer project), and `PORT()` capped at 16 arguments where `ctrl.gamemite` passes **18** — a constant |
| **GNR_6** | `PAGE WRITE` and `LOAD PNG`. Its CSUB gets recoded in BASIC per the standing rule. Also two source fixes: `DefineFont 9`→10, and `Sub addgem` closed with `Exit Sub` and no `End Sub` |

Everything else the corpus needed has shipped — see below and
[[pc3-v016-unreleased]].

**A WARNING ABOUT THIS FILE'S OWN NUMBERS.** A fatal error stops the
later translation passes, so a game that stops early reports *nothing
else*. I have been caught by this twice: once reading "eight games
translate clean" (they had died on line one of their first `DIM`), and
once reporting "GNR_6: 0 lines left" when its `unterminated routine
block` had simply hidden everything. **Before believing any per-game
count, lift the current blocker in a throwaway copy of `mmb2c.py` and
translate again.**

## Done 2026-08-16 (v0.16 material)

Items 1, 2, 7 and the `CONST` defect, plus `MM.INFO$()`, the flat
`MM.FONTHEIGHT` / `MM.FONTWIDTH` / `MM.HPOS` / `MM.VPOS`, and
`MM.DEVICE$` shortened to `"Fuzix"` with the board moved to
`MM.INFO(PLATFORM)`.

Measured the same way as the survey above — the two remaining blockers
(run-time `DIM` bounds, `DefineFont` into slots 1-9) lifted in a
throwaway copy, so the comparison is like for like:

| | before | after |
|---|---|---|
| games producing C that compiles | 9 of 16 | **15 of 16** |

The `CONST` fix alone accounts for the whole Cluster B jump: 3D-maze,
Circle, FileManager, Kingdom and Pico-Vaders all compile now. Only
Lazer-Cycle still fails, on a `break` left outside its loop by an
unrelated commented-out line - the cascade class, not a missing feature.

Two defects fell out of it, both of which **no test had ever
exercised**:

* a global `CONST` and a `LOCAL` of the same name generated the same C
  name, because a `#define` has no scope. Global constants now get their
  own `k_` prefix.
* `MM.CMDLINE$` segfaulted under bcrun before the program's first
  statement: bcrun dispatches the entry with nothing pushed, so a `main`
  declared with two parameters read a frame two slots short. Nothing had
  caught it because no test in the suite used `MM.CMDLINE$` - the only
  thing that asked for the arguments - until `MM.INFO(PATH)` did. The
  `--fcc` output now emits `main(void)`.

Gates: make check 0, cgate byte-identical, tokgate 72/72, fcctests
50/50, qemu 51/51. `tests/mminfo.bas` covers all of it and is in every
gate. **Board-verified 2026-08-16.**

## Then, in the same run

* **GUI BITMAP** (`mmb_gui.h`) — bit order copied from the firmware,
  not from its manual: `bitmap[n/8] >> ((h*w - n - 1) % 8)`, which
  equals "bit 7 first" **only when `h*w` is a multiple of 8**. 16×8 is,
  so the Game\*Mite sprites are unaffected. The integer form is
  little-endian. `tests/guiharness.c` compares 9,639 drawings against
  the firmware's own loop.
* **`LINE` with a width** (`mmb_gfx_line.h`) — four algorithms picked by
  shape, and the *sign* of the width decides whether it hangs off one
  side or is centred. `tests/lineharness.c`, 378 wide lines.
* **run-time `DIM a(n)` and `REDIM`** — see [[mmb2c-runtime-array-shape]]
* **`I2C` READ/WRITE/CHECK + `MM.I2C`**, and I2C2 changed to match —
  see [[pc3-i2c0-fixed-bus]]
* **`PEEK(VARADDR)` and `POKE`** — `VARADDR` lives in the translator,
  not the header: it needs the symbol, not an address. A string gives
  its **length byte**, so `PEEK(BYTE a)` is the length and `a+1` the
  first character, exactly as on a PicoMite.
* cc2 `MAXFIX` 8192→16384 and `MAXLAB` 3072→8192, PSRAM-backed, plus
  every table overflow now names the limit it hit.

Gates at the end of the run: make check 54, cgate byte-identical,
tokgate 76, fcctests 54/54, qemu 55/55.

## Recommended first slice

Items 1, 2 and 7 — `MM.INFO()`, `GPn`, `KEYDOWN()` — plus stripping the
menu. All three are small, all three are wanted well beyond this
corpus, and item 2 closes a silent-wrong-pin hazard that exists today.
That should bring **five or six of Cluster A up on the keyboard**,
which is enough to know whether the rest is worth doing before
committing to Cluster B.

Then items 3–6 as one piece of work, because they are one library and
they arrive together or not at all.

See [[pc3-games-plan]], [[mmbasic-coverage-plan]],
[[different-worse-than-missing]].

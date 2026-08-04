# What else of MMBasic could mmb2c translate?

A sweep of every command and function in the PicoMite manual — 166 distinct
command keywords, 91 distinct function keywords — sorted by whether a plain
C translation can honestly reproduce the behaviour.

Where we are now: **60 of the 91 function keywords** and **36 of the 166
command keywords** are translated. The command figure looks worse than it is;
roughly 75 of those keywords are graphics, sound or peripheral drivers.

`CHOICE` is already in, as of the built-ins round — `CHOICE(a=b, 4, 5)`
compiles to a C ternary with the branch types checked, and the string form
picks between two `char*`. So are `BIT`, `BYTE`, `BOUND`, `TRIM$`, `FIELD$`,
`FORMAT$`, `RGB` and the date/time family.

---

## Tier A — DONE

All of these are now translated and covered by `tests/t7.bas`.
Left here as a record of what each one compiles to.

| feature | what the C looks like | rough size |
|---|---|---|
| **`DATA` / `READ` / `RESTORE`** (+ `READ SAVE`/`RESTORE`) | every `DATA` item becomes an entry in a static const table; a global cursor; `READ` pulls and converts to the target's type. `RESTORE label` sets the cursor to that label's index. `READ a()` fills a whole array. | ~150 lines |
| **`SORT array() [,index()] [,flags] [,start] [,count]`** | `qsort` per element type, with the documented flag bits: bit0 reverse, bit1 case-insensitive, bit2 empty strings last. The index array records the pre-sort positions so parallel arrays can follow. | ~120 lines |
| **`CONTINUE FOR` / `CONTINUE DO`** | C `continue`. Falls out correctly: it binds to the `for`, and for `DO…LOOP UNTIL` (a C `do{}while`) it jumps to the test, which is exactly "skip to the end, then test". | ~10 lines |
| **`INC var [,incr]`** | `v += x`, or `mm_scat` for the string form. | ~20 lines |
| **`CAT s$, n$`** | `mm_sset(s, mm_scat(s, n))`. | ~10 lines |
| **`ON nbr GOTO`** | a C `switch` over the label list. `ON nbr GOSUB` needs Tier B's return stack. | ~25 lines |
| **`ERROR ["msg"]`** | `mm_error()` — already there, just needs the statement. | ~10 lines |
| **`PAUSE ms`** | `nanosleep`, with a busy-wait fallback where it is missing. | ~15 lines |
| **`TIMER = n`, `DATE$ =`, `TIME$ =`** | a clock offset the existing date/time functions add in. | ~30 lines |
| **`ARRAY SET/ADD`, `MATH SET/SCALE/ADD`** | element-wise loops; rank and extent are known at compile time. | ~80 lines |
| **`MATH(SUM/MEAN/MAX/MIN/SD/MEDIAN a())`** | array reductions, `MAX`/`MIN` optionally writing back the index. | ~60 lines |
| **`ERASE` / `CLEAR`** | *partial and worth flagging*: our variables are static C storage, so these can zero a variable but cannot free it. Honest behaviour, documented, not a silent lie. | ~20 lines |

`DATA`/`READ` is the one I would do first — it is the most common thing in an
ordinary BASIC program that currently fails outright.

---

## Tier B — real value, moderate work

| feature | the catch |
|---|---|
| ~~**The `LONGSTRING` family**~~ **DONE** — `APPEND CLEAR CONCAT COPY LEFT MID RIGHT LOAD PRINT REPLACE RESIZE SETBYTE TRIM UCASE LCASE`, plus `LLEN LGETSTR$ LGETBYTE LINSTR LCOMPARE LINPUT` | Conceptually easy: a long string is just a byte buffer living inside an `INTEGER` array, which in C is a `MMINTEGER[]` we can alias to `char*`. About 20 small commands. This is what unlocks text longer than 255 characters, and `LONGSTRING PRINT #n` pairs with the file handling just added. ~250 lines. |
| **`TYPE` / structures** | Very mechanical — `TYPE…END TYPE` becomes a C `struct`, members map directly, struct assignment is `=`. But it needs a **tokenizer change**: today `a.b` lexes as one identifier, because MMBasic allows dots in variable names. Disambiguating `alarm.time` from a variable literally called `alarm.time` needs the symbol table consulted during lexing. That is the only awkward part. |
| ~~**`GOSUB` / `RETURN`, `ON nbr GOSUB`**~~ **DONE** (cross-function GOSUB refused) | Needs a return-address stack: push an id, `goto` the label, and at `RETURN` a `switch` on the top of stack jumps back. Mechanical but ugly generated C. The manual itself calls `GOSUB` legacy and recommends `SUB`, so this is for old programs only. |
| **`REDIM [PRESERVE]`** | Requires heap-allocated arrays instead of the current static ones — a genuine change to the array model, and it would put `malloc` into generated code that currently has none. Worth doing only if you actually use it. |
| **`MATH` matrix and vector** — `M_MULT M_INVERSE M_TRANSPOSE M_DETERMINANT V_CROSS V_NORMALISE MAGNITUDE DOTPRODUCT CORREL CHI CROSSING`, plus `MATH CRC` and `BASE64` | Pure arithmetic, no platform dependency. Each is small; the set is large. Good candidates to add on demand rather than all at once. |
| **`ARRAY SLICE` / `ARRAY INSERT`, `MATH C_*`** | Index arithmetic over known dimensions. Mechanical. |
| **`INKEY$`, `KEYDOWN`** | Non-blocking console reads need `termios` on POSIX (or `conio` on Windows) — same treatment as the directory functions: guarded, with a clean runtime error when the build excludes them. |
| **`ON ERROR SKIP/IGNORE`, `MM.ERRNO`, `MM.ERRMSG$`** | Cross-cutting: today `mm_error()` prints and exits. Soft failure means either `setjmp`/`longjmp` or a checked error flag after every runtime call. Doable, but it touches everything, so it deserves its own pass. |

---

## Tier C — possible, but I would want your steer first

* **`MM.` read-only variables** (`MM.DEVICE$`, `MM.VER`, `MM.CMDLINE$`,
  `MM.HRES`, `MM.ERRNO`…). Trivial to add; the question is what they should
  *say* off-Pico. `MM.DEVICE$` = `"mmb2c"`? `MM.CMDLINE$` from `argv`?
* **`VAR SAVE` / `VAR RESTORE` / `VAR CLEAR`.** Flash-backed variables map
  naturally onto a small file. Identical from the program's point of view,
  and now cheap given the file layer exists.
* **`PEEK` / `POKE` / `MEMORY COPY|SET|PACK`.** The `PEEK(VAR x)` and
  array-address forms translate fine. Raw numeric addresses would become
  undefined behaviour instead of a clean error, which is a meaningful step
  down in safety from what you have now.
* **`CALL(fname$, args…)`.** Indirect call by name — a generated dispatch
  table keyed on the routine name. Works, but only for routines with
  compatible signatures.
* **`JSON$`.** Needs a parser; `cJSON` is already vendored in the PicoMite
  tree under a permissive licence and could come along.
* ~~**`CLS`, `COLOUR`, `FONT`, console positioning.**~~ **DONE**, and not
  by driving a VT100 after all: on the PC3 they reach the display through
  the kernel, so `CLS [colour]`, `COLOUR`, `FONT #n [,scale]` and
  `PRINT @(x,y)` paint real pixels. On a host with no display they are
  silently nothing, which is what keeps the gates meaningful.
* **`CSUB`.** The BASIC embeds ARM machine code, which is meaningless here —
  but a `CSUB` *declaration* could map to an `extern` C function you link in
  yourself, which is arguably nicer than the original.

---

## Tier D — deliberately out

A C translation cannot honestly provide these, and pretending otherwise
would be worse than the current clear error:

* **Peripherals** — `PIN PORT PWM SERVO SETPIN I2C SPI ONEWIRE PIO ADC IR
  WS2812 STEPPER TMC22XX HUMID TEMPR DISTANCE PULSIN CAMERA KEYBOARD KEYPAD
  MOUSE GAMEPAD WII RTC WATCHDOG CPU FLASH SLEEP BITBANG BITSTREAM`
* **Graphics and video** — `SPRITE TILE TILEMAP MAP TURTLE MANDELBROT RAY
  DRAW3D GUI DEFINEFONT RESOLUTION BACKLIGHT LCD TOUCH CLICK GETSCANLINE`

  This entry used to say "every drawing command", and that is no longer
  true: the PC3 kernel draws, so `MODE PIXEL LINE BOX CIRCLE CLS COLOUR
  TEXT FONT FRAMEBUFFER BLIT` and `PRINT @` are translated and run on
  hardware.  What remains here is the part that needs state a translator
  has no place to keep - sprites, tile maps, a GUI toolkit - or hardware
  the PC3 does not have.
* **Sound** — `PLAY`
* **Interrupts and background timing** — `SETTICK ON KEY ON PS2 INTERRUPT
  IRETURN MATH PID MATH SENSORFUSION ONESHOT`
* **Editor and REPL** — `EDIT LIST NEW RUN SAVE LOAD AUTOSAVE FM MEMORY
  LIBRARY XMODEM YMODEM CHAIN EXECUTE TRACE`
* **`EVAL`** — evaluating a string as BASIC at run time needs the
  interpreter, which is the one thing a translator has thrown away.

---

## Suggested order

~~1. `DATA`/`READ`/`RESTORE`~~ — done.
~~2. The rest of Tier A~~ — done.
~~3. `LONGSTRING`~~ — done, along with `GOSUB`/`RETURN`.
4. `TYPE`/structures — the largest single win left, and the only one that
   forces a tokenizer change.

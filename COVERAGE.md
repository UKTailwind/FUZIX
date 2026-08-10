# What else of MMBasic could mmb2c translate?

A sweep of every command and function in the PicoMite manual — 166 distinct
command keywords, 91 distinct function keywords — sorted by whether a plain
C translation can honestly reproduce the behaviour.

Where we are now: **71 function keywords** and **~50 command keywords**
are translated (2026-08-07 count, including the graphics, GPIO and PLAY
work below). The command figure looks worse than it is; a large share of
the remainder is peripheral drivers this machine has no path to.

A full re-review against the firmware's own keyword tables — including
which build category every remaining keyword belongs in — is in
`REVIEW-COVERAGE-2026-08-07.md`, which supersedes the triage below where
they disagree.

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
| ~~**`TYPE` / structures**~~ **DONE** (2026-08-07) | The feared tokenizer change never happened: a dotted name is already one token, so the firmware's own rule — split at the first dot at *lookup* time, the prefix wins if it names a struct variable — needed no lexer change beyond `.` as an operator for `arr(i).member`.  The firmware's byte layout is reproduced exactly (TYPE-SPEC.md), member strings are bounded Pascal fields (`mm_ssetm`), structs live in the heap block, parameters pass by reference.  `STRUCT COPY/CLEAR/SWAP` and compile-time `STRUCT(SIZEOF/OFFSET/TYPE)` included; `SORT/SAVE/LOAD/PRINT/EXTRACT/INSERT/FIND`, struct-returning functions and struct-array parameters/initialisers are refused with messages, as are the interpreter's two nested-member-copy overrun defects. |
| ~~**`GOSUB` / `RETURN`, `ON nbr GOSUB`**~~ **DONE** (cross-function GOSUB refused) | Needs a return-address stack: push an id, `goto` the label, and at `RETURN` a `switch` on the top of stack jumps back. Mechanical but ugly generated C. The manual itself calls `GOSUB` legacy and recommends `SUB`, so this is for old programs only. |
| **`REDIM [PRESERVE]`** | Requires heap-allocated arrays instead of the current static ones — a genuine change to the array model, and it would put `malloc` into generated code that currently has none. Worth doing only if you actually use it. |
| **`MATH` matrix and vector** — `M_MULT M_INVERSE M_TRANSPOSE M_DETERMINANT V_CROSS V_NORMALISE MAGNITUDE DOTPRODUCT CORREL CHI CROSSING`, plus `MATH CRC` and `BASE64` | Pure arithmetic, no platform dependency. Each is small; the set is large. Good candidates to add on demand rather than all at once. |
| **`ARRAY SLICE` / `ARRAY INSERT`, `MATH C_*`** | Index arithmetic over known dimensions. Mechanical. |
| ~~**`INKEY$`**~~ **DONE** (`mm_inkey()`, termios-guarded, returns MMBasic's key codes) · **`KEYDOWN`** | `KEYDOWN` needs a key-state table from the kernel, which INKEY$'s one-byte read does not provide — a small kernel ioctl plus a wrapper when wanted. |
| **`ON ERROR SKIP/IGNORE`, `MM.ERRNO`, `MM.ERRMSG$`** | Cross-cutting: today `mm_error()` prints and exits. Soft failure means either `setjmp`/`longjmp` or a checked error flag after every runtime call. Doable, but it touches everything, so it deserves its own pass. |

---

## Tier C — possible, but I would want your steer first

* **`MM.` read-only variables** (`MM.DEVICE$`, `MM.VER`, `MM.CMDLINE$`,
  `MM.ERRNO`…). Trivial to add; the question is what they should
  *say* off-Pico. `MM.DEVICE$` = `"mmb2c"`? `MM.CMDLINE$` from `argv`?
  `MM.HRES` and `MM.VRES` are DONE — they ask the kernel.
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

* **Peripherals** — `PORT I2C SPI ONEWIRE PIO IR
  WS2812 STEPPER TMC22XX HUMID TEMPR DISTANCE PULSIN CAMERA KEYBOARD KEYPAD
  MOUSE GAMEPAD WII RTC WATCHDOG CPU FLASH SLEEP BITBANG BITSTREAM`

  `PWM` and `ADC` have left this list.  `PWM slice, freq, duty [, duty2]`
  and `PWM slice, OFF` translate, with `SETPIN pin, PWM` attaching a pin
  - MMBasic's split, because one slice drives two pins.  The arithmetic
  is cmd_pwm's step for step, including the halving loop that trades
  counter bits for clock divider below ~5.7kHz and the negative-duty
  polarity bit.  `SETPIN pin, PWM` claims the SLICE as well as the pin:
  twelve slices cover forty-eight pins on the RP2350B, so GP34 and GP42
  are one channel and a pin claim alone would not have caught it.
  `PWM SYNC` is not translated.  Scope-verified on GP0 at 50Hz, 1kHz,
  10kHz and 100kHz, and inverted.

  `SERVO slice, position [, position2]` and `SERVO slice, OFF` too - a
  50Hz frame with MMBasic's `duty = 5 + position * 0.05`, so 0 is a 1ms
  pulse, 50 is 1.5ms and 100 is 2ms, over-travel -20 to 120.
  Scope-verified at all five.

  Omitting the second duty or position LEAVES THAT CHANNEL ALONE, as
  MMBasic does.  The first version here zeroed it, which would have
  stopped a servo on channel B every time channel A was set - two
  outputs share a slice, and SERVO is what makes that common.

  The rest:
  `SETPIN pin, DIN|DOUT|AIN|ARAW|INTH|INTL|INTB|OFF`, `PIN(n) =` and the
  `PIN(n)` function are translated (mmb_gpio.h and mmb_int.h), using
  GPIO numbers rather than connector-pin numbers.  The input modes take
  MMBasic's optional `PULLUP`/`PULLDOWN` (last argument, after the
  handler for the interrupt forms), defaulting to neither as it does;
  hysteresis is always on for inputs, which is not an option there
  either.  `AIN` returns volts through MMBasic's own
  ten-sample sort-and-discard filter and `ARAW` the raw count; both need
  an ADC pin, GP40-GP46 on the PC3's header.

  Two things about this differ from MMBasic and are deliberate.
  **`PIN()` is a float in every mode** where MMBasic returns an integer
  for digital pins and `ARAW`: translated C fixes the type when it is
  generated and nothing then knows what mode a pin will be in, so one
  type has to cover both, and a double holds 0, 1 and every 12-bit
  count exactly.  Nothing observable changes.  And **pins are owned** -
  `SETPIN` claims from the kernel's pin lock, a pin another program
  holds is refused, and the claim is released and the pin reset when
  the program ends however it ends.

  There is no syscall left on the pin path: the claim is one ioctl at
  `SETPIN`, and after that a pin is a register access - about ten
  nanoseconds against 1.5 us, which is the difference between being
  able to bit-bang a protocol in BASIC and not.
* **Graphics and video** — `SPRITE TILE TILEMAP MAP TURTLE MANDELBROT RAY
  DRAW3D GUI DEFINEFONT RESOLUTION BACKLIGHT LCD TOUCH CLICK GETSCANLINE`

  This entry used to say "every drawing command", and that is no longer
  true: the PC3 kernel draws, so `MODE PIXEL LINE CIRCLE BOX RBOX
  TRIANGLE ARC CLS COLOUR TEXT FONT MAP FRAMEBUFFER` and `PRINT @` are
  translated and run on hardware (`TRIANGLE` in its drawing form;
  `SAVE`/`RESTORE` need the interpreter's blit buffers).  `BLIT` still
  needs a block pixel-read ioctl.  What remains here is the part that
  needs state a translator has no place to keep - sprites, tile maps, a
  GUI toolkit - or hardware the PC3 does not have.
* **Sound** — `PLAY TONE WAV FLAC MOD MIDI SAMPLE EFFECT PAUSE NEXT PREVIOUS`

  Not all of `PLAY`, though: `PLAY MP3`, `PLAY VOLUME` and `PLAY STOP` are
  translated and play real audio on the PC3, where the decoder is a
  separate process and the kernel owns the I2S engine.  What remains
  needs the interpreter's idle loop - MMBasic refills its audio buffers
  from there - or a synthesiser the kernel does not have.
* **Interrupts and background timing** — `SETTICK ON KEY ON PS2 INTERRUPT
  IRETURN MATH PID MATH SENSORFUSION ONESHOT`

  The PIN half is done: `SETPIN pin, INTH|INTL|INTB, handler` translates,
  with the handler an ordinary parameterless SUB.  It is MMBasic's own
  algorithm - a POLL after every statement comparing each armed pin's
  level against its level at the previous check (MM_Misc.c:10153) - so
  the latency is one statement, a pulse shorter than a statement is
  missed, interrupts do not nest, one fires per statement boundary, and
  the error state is saved, cleared and restored round a handler.  See
  PLAN-interrupts.md.

  `IRETURN` is NOT translated and does not need to be: for a SUB target
  MMBasic builds one itself as the return address of the GOSUB it fakes
  (MM_Misc.c:10205), so END SUB performs the interrupt return there too.
  Written IRETURN belongs to the label and line-number targets, which are
  refused - compiled code cannot jump into the middle of a function.

  `SETTICK` is done too: all four timers, `PAUSE`/`RESUME`, `0, 0` to
  turn one off, and MMBasic's catch-up - a handler slower than its own
  period drops the missed firings rather than queueing them, keeping the
  phase.  It holds a MICROSECOND DEADLINE where MMBasic counts
  milliseconds in an interrupt, which is the one deliberate divergence
  and it is in our favour: MMBasic fires when its count is *greater
  than* the period, so `SETTICK 100` runs at 101ms there and at 100ms
  here.  Board-measured: 20 x 100ms in 1999.98ms.

  `ON KEY` is done, both forms: the any-key form fires while a key is
  waiting and LEAVES it for `INKEY$`, the specific form fires on one
  code and EATS it so it never reaches `INKEY$`.  That asymmetry is the
  point of having both and is MMBasic's (PicoMite.c:932-935).  Specific
  is checked before any-key, as there.

  One divergence: the console is looked at **at most every 5ms** rather
  than every statement - looking is a syscall and a poll site runs after
  every statement.

  Building this also FIXED `INKEY$`, which had never seen a keystroke at
  the PC3 console.  The console line editor (`lineedit.c`) swallows
  every key while the terminal is in `ICANON|ECHO` and releases the
  whole line at Enter, and `mm_inkey` used to flip to raw for the few
  microseconds of one read - a window open only DURING the read, while
  every key is typed at some other moment.  The runtime now takes the
  terminal on the first `INKEY$` or armed `ON KEY` and KEEPS it, giving
  it back around `INPUT` (which wants the editor and the echo) and at
  exit.  Board-verified: a whole sentence typed on the USB keyboard,
  every character read as it was pressed.

  The restore is registered with `atexit`, not just put in `mm_end` - a
  translated program's `main` ends with a plain return, and leaving the
  terminal raw hands the shell an instant EOF, which it takes for a
  hangup and LOGS THE USER OUT.  Found the hard way.

  That leaves `ON PS2 INTERRUPT MATH PID SENSORFUSION ONESHOT` from this
  group, none of which have anything to notify on this machine.
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

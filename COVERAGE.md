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
| ~~**`ON ERROR SKIP/IGNORE`, `MM.ERRNO`, `MM.ERRMSG$`**~~ **DONE** | It did get its own pass, and it did touch everything. The checked-flag route rather than `setjmp`: generated code binds a two-int state (`mm_err_bind`), and the translator emits arithmetic checks *only* into programs that actually trap, so a program that never says `ON ERROR` pays nothing — that gating was worth 11.9% on the benchmark. `tests/onerror.bas` covers it. |

---

## Tier C — possible, but I would want your steer first

* **`MM.` read-only variables** (`MM.DEVICE$`, `MM.VER`, `MM.CMDLINE$`,
  `MM.ERRNO`…). Trivial to add; the question is what they should
  *say* off-Pico. `MM.DEVICE$` = `"mmb2c"`? `MM.CMDLINE$` from `argv`?
  `MM.HRES` and `MM.VRES` are DONE — they ask the kernel.
* **`VAR SAVE` / `VAR RESTORE` / `VAR CLEAR`.** Flash-backed variables map
  naturally onto a small file. Identical from the program's point of view,
  and now cheap given the file layer exists.
* ~~**`PEEK`**~~ **DONE** — `BYTE`, `SHORT`, `WORD`, `INTEGER` and
  `FLOAT`, with MMBasic's alignment check and MMBasic's message. The
  worry recorded here was that raw numeric addresses become undefined
  behaviour rather than a clean error, and that is exactly what they
  do; what changed is that there is now a reason worth it.
  `MM.INFO(FONT ADDRESS n)` hands back the address of a built-in font
  in the kernel's flash, and without `PEEK` that address is useless —
  a program driving its own SPI panel could ask where MMBasic's glyphs
  were and not read them. MMBasic on a PicoMite is no safer, and the
  alternative was every such program shipping its own copy of a font.

  `PEEK(VAR x)`, `PEEK(VARADDR x)` and `PEEK(CFUNADDR …)` are *not*
  done: those ask about a variable rather than an address, which needs
  the symbol table rather than a value.
* **`POKE` / `MEMORY COPY|SET|PACK`.** Still open, and a bigger step
  than `PEEK` was — a bad read kills one program, a bad write can take
  the kernel with it, and there is no MMU to disagree.
* **`MM.INFO(...)`** translates `FONT ADDRESS n` and nothing else. The
  other options that make sense on this machine already have flat
  spellings (`MM.HRES`, `MM.VRES`, `MM.DEVICE$`, `MM.VER`,
  `MM.ERRNO`); the rest describe a filesystem and a display that are
  not this one. An unsupported option names itself in the error.
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

* **Peripherals** — `PIO IR
  WS2812 STEPPER TMC22XX HUMID DISTANCE PULSIN CAMERA KEYBOARD KEYPAD
  MOUSE GAMEPAD WII WATCHDOG CPU FLASH SLEEP BITBANG BITSTREAM`

  `SPI`, `I2C2`, `RTC GETREG`/`RTC SETREG`, `PORT`, `PULSE`, `ONEWIRE`
  and `TEMPR` have left this list. The last two are the interesting
  departure: one-wire needs 60 µs slots with a 10 µs sample point, and
  was impossible while a pin cost an ioctl at 1.488 µs. Once pins
  became register writes it became ordinary code.

  `SETPIN p1, p2, p3, SPI` then `SPI OPEN speed, mode [, bits]` gives
  the first controller, with `WRITE`, `READ`, `CLOSE` and the `SPI()`
  function.  The three pins go in any order — the RP2350 fixes each
  pin's role, `(pin AND 8) = 0` selecting SPI0 and `pin AND 3` the
  signal, which is how MMBasic resolves them too.  `SPI2` stays out:
  that is the second controller and here it is the SD card.  Chip
  select is the program's, as it is on a PicoMite.  `MM.SPISPEED`
  reports the clock actually achieved, which is rarely the one asked
  for.

  `SETPIN sda, scl, I2C2` then `I2C2 OPEN speed, timeout` gives the
  second controller on header pins — `GP38/GP39` or `GP42/GP43`, the
  only pairs the RP2350 can mux to I2C1.  `WRITE` and `READ` take
  MMBasic's forms: a list of byte expressions, a whole numeric array
  written `a()`, or a string; and the option word's bit 0 HOLDS the bus
  so the next transfer is a repeated START, which is what a device
  needing a genuine combined transfer requires.  The `timeout` is
  MMBasic's, in milliseconds, `0` or `100` and up — except that `0`
  means a five-second cap rather than "no timeout", because a
  non-preemptive kernel that waits for ever takes the console and the
  display down with the program.

  `I2C` (the fixed bus, `GP20/GP21`) stays out: that is the QWIIC socket
  *and* the DS3231 the system clock runs on, and the kernel polls it
  from interrupt context.  `RTC GETREG`/`SETREG` reach the clock's own
  registers through the kernel instead, which is how an alarm is armed
  since MMBasic has no alarm command either — write `&H07`–`&H0A` and
  set `INTCN|A1IE` in `&H0E`, then `SETPIN 32, INTL, handler`.  A write
  cannot clear `EOSC`: stopping a battery-backed oscillator outlives the
  power cycle.

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

  `RTC GETREG reg, var` and `RTC SETREG reg, value` reach any DS3231
  register, which is MMBasic's own interface - it has GETREG and SETREG
  and NO alarm command, so arming an alarm is writing 0x07-0x0A and then
  INTCN|A1IE into 0x0E, exactly as a PicoMite program does.  Not through
  /dev/i2c, which refuses 0x68 because the chip is the system clock; the
  kernel does it, and refuses one thing - a write cannot set EOSC, since
  stopping a battery-backed oscillator outlives the power cycle.
  Board-verified: eight once-a-second alarms caught on GP32.
  `RTC GETTIME`/`SETTIME` are not translated - the system clock already
  tracks the chip, and `setdate` is the way to set it.

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

  **The data arguments are one implementation, as MMBasic's are.**
  `GetCommsTxData`, `GetCommsRxDest` and `PutCommsRxData` serve I2C,
  SPI and one-wire there; the same forms are shared here (`mmb_comms.h`)
  rather than written out per bus. Both directions take a list of
  expressions (the count must match, as MMBasic checks), a string, or a
  whole numeric array; a read also takes a **list of lvalues**, one per
  value. Destinations are validated *before* the transfer, because a
  read addresses a device and may advance a register pointer inside it.
  The buffer holds values rather than bytes — an SPI word can be 16
  bits, which is why MMBasic's is `unsigned int` too.

  **One-wire is the third caller of that layer**, which is what it was
  built for. `ONEWIRE RESET|WRITE|READ pin, flag, count, …` takes the
  same data forms as the other two buses and needed no new argument
  handling at all, because MMBasic's `owWrite`/`owRead` call the same
  two functions `I2C.c` and `SPI.c` do. `MM.ONEWIRE` holds what the
  last reset saw, and the flag bits are MMBasic's: 1 reset first, 2
  reset after, 4 single bits, 8 strong pull-up.

  The slots are bit-banged in the program — a slot is 60 µs and its
  sample point 10, so this was impossible while a pin cost an ioctl.
  **What cannot be copied is MMBasic's `disable_interrupts_pico()`**
  around a byte: a userland program cannot mask interrupts and should
  not be able to. The kernel is non-preemptive, so nothing takes the
  processor between two instructions here, but a timer interrupt can
  still stretch a slot. One-wire tolerates a *long* slot rather than a
  short one, so a stretched write still reads correctly; a program that
  cares should check the CRC the device provides.

  **`TEMPR` sleeps where MMBasic spins**, and that is the one
  deliberate difference. A 12-bit DS18B20 conversion is 750 ms, and
  `fun_ds18b20` either calls `uSec(200000)` outright or loops on its
  timer — which costs nothing on firmware with one program to run.
  This machine has others, so the wait is slept, and through the
  serviced wait, so a `SETTICK` handler keeps firing while a
  temperature is being measured. `TEMPR START` returns at once (6 ms
  measured) and the reading collects the result later.

  **`LONGSTRING a()` is an extension**, and the reason SPI was
  re-opened: a BASIC string stops at 255 bytes, a 240-pixel RGB565 row
  is 480, and a whole frame is 153,600, so a row could not be held in
  BASIC at all and every drawing program carried a chunking loop. It
  must be spelled out, because a long string *is* an integer array —
  written `a()` it is a numeric array and sends one byte per eight-byte
  cell, which is MMBasic's behaviour and stays. **It is not a speed
  feature**: measured on the board, one call per row against 100 pixels
  a call is 30 ms against 32 at 24 MHz and 12 against 13 at 62.5 — the
  bus dominates, and what changes is what a program can express.

  `PORT(pin, nbits [, pin, nbits]...)` translates in both directions -
  as a statement it writes several pins as one number, as a function it
  reads them back (mmb_port.h).  The first pin of a group is the LEAST
  significant bit, which is MMBasic's order and not the intuitive one:
  `PORT(0,8) = 1` lights GP0.  **Every pin in a bank changes on the same
  clock edge**, which is the whole reason to prefer it over eight
  `PIN()` writes: the differing bits are worked out first and posted as
  one masked store, so a bus never carries an intermediate value.  A
  port spanning GP31/GP32 takes one store per bank, as it must - there
  is no register covering both, and MMBasic's `gpio_xor_mask64` does the
  same two.  Reading is one snapshot of all 48 pins for the same reason.

  `PULSE pin, width_ms` translates (mmb_pulse.h).  It INVERTS the pin
  for the width rather than driving it high, which is MMBasic's
  behaviour.  Under 3 ms it blocks and the width is exact; at 3 ms and
  above it returns at once and the pin flips back later.  **Where the
  later flip happens is a divergence**: MMBasic ends a long pulse from a
  hardware timer, and Fuzix has no sub-second interval timer to hang one
  on, so it ends at the next PAUSE, the next PULSE, or the next
  statement boundary in a program that also uses interrupts.  A program
  that starts a 500 ms pulse and then computes for a second without
  pausing holds the pin for the second.

  A related fix came with it: **PAUSE now services interrupts while it
  waits**, which it did not before.  MMBasic's `cmd_pause` checks
  interrupts every time round its loop, and a program that arms a
  `SETTICK` usually has a main loop of little but PAUSE - so a PAUSE
  that ignored the tick meant the handler never ran at all.  The wait is
  now sliced with the poll between the slices, and the slice is asked
  for rather than assumed: the shortest armed tick period, or the time
  left on a running pulse.  A slow tick still sleeps and costs nothing;
  a fast one spins, exactly as MMBasic does, and only while pausing.

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
~~4. `TYPE`/structures~~ — done.

See `NEXT.md` for the current ranked queue.

---

# The complete outstanding list

Generated 2026-08-12 by diffing MMBasic's own `AllCommands.h` tables
against `mmb2c.py`'s dispatch and `BUILTINS`, then classified by hand.
The counts exclude MMBasic's two empty placeholder rows, its comment
artefacts and the underscore-prefixed PIO assembler directives.

| | in MMBasic | translated | outstanding |
|---|---|---|---|
| commands | 208 | 105 | **103** |
| functions | 93 | 65 | **28** |

`POLYGON`, `BEZIER` and `FILL` came off this list on 2026-08-12 and
are struck through below.

Names below are MMBasic's. Where a keyword is ambiguous the handler it
binds to is given, because `Set`, `Wait`, `Mov` and friends are not
what they look like.

## Category 1 — finish what is already there

These complete a family that is otherwise done, and none needs a new
mechanism. This is the category worth clearing before anything else,
because each item is an inconsistency a user meets by accident.

**Drawing.** The primitives are there — `LINE BOX CIRCLE RBOX TRIANGLE
ARC PIXEL TEXT CLS` — and these are the gaps in them:

* ~~**`POLYGON`**~~ **DONE.** The fill is crossing-based — every edge
  crossing on a row, sorted, filled between alternate pairs. It was
  written first by reusing `TRIANGLE`'s per-row min/max extent tables,
  which is exact for a triangle because a triangle is always convex and
  cannot express a gap: an arrowhead's notch filled solid. The
  multi-polygon form (a vertex *count array*) is refused by name.
* ~~**`FILL`**~~ **DONE**, both modes. The cost model did differ, and
  the answer was to fix the asymmetry rather than work around it: there
  were seven ways to write pixels and one to read. **`GFXIOC_BLITRD`**
  is now the inverse of `GFXIOC_BLIT` — native bytes out of the draw
  target, MMBasic's `ReadBufferFast` to `GETPIXEL`'s `ReadBuffer` — so
  a row costs one crossing instead of 320. Circle interior 6 ms, whole
  screen 75 ms, against ~230 ms a pixel at a time. `GFXIOC_COLOUR` also
  returns the index it mapped to now, because a program scanning raw
  bytes cannot otherwise know what a colour looks like in them.
* ~~**`BEZIER`**~~ **DONE** — `PlotBezier` transcribed, including its
  step count (three times the bounding-box diagonal, clamped to
  [10, 2000]) and its run-length coalescing. Sixteen control points
  maximum with an error past it, where MMBasic walks off three stack
  arrays.

**Pins.** `SETPIN`/`PIN` are done; the grouped forms are not:

* **`PORT(...)= v`** (`cmd_port`) and **`PORT(...)`** (`fun_port`) —
  read or write several pins as one value, given as
  `(startpin, nbits, startpin, nbits, …)`. MMBasic builds a mask and
  does it in one `gpio_get_all64`/set. On this port pins are already
  userland register writes through `<sys/pc3io.h>`, so this is mask
  arithmetic and one store — and it is what a parallel LCD or any
  bus-like device needs.
* **`PULSE`** (`cmd_pulse`) — a timed pulse on a pin.

**Strings and small odds.** All have their function halves already:

* ~~**`MID$(s,n,m) = t`**~~ — **done**, and it always was: it is
  handled in the assignment parser rather than as a statement keyword,
  which is why the generated outstanding list still names it.
* ~~**`LMID(a(), start [, num]) = s$`**~~ — **done** (`mm_ls_lmid`).
  Worth knowing that it is a **splice and not an overwrite**: `num`
  bytes come out and the string goes in, so the long string grows or
  shrinks unless the two lengths match, and leaving `num` out means
  "as long as the replacement". One divergence, deliberate: MMBasic's
  bound is off by one (`start + (num - 1) - 1 > currentlength`) and
  lets a selection run a byte past the end, where the tail it then
  moves is minus one byte long. Refused here.
* ~~**`BIT(x,n) = v`**, **`BYTE(s$,n) = v`**, **`FLAG(n) = v`**,
  **`FLAGS = v`**~~ — **done**, with `FLAG(n)` and `MM.INFO(FLAGS)`
  reading them back. All four reach into a variable rather than
  replacing it. MMBasic checks the target's type at run time ("Not an
  integer", "Not a string"); the translator knows an lvalue's type when
  it generates the call, so those are refused at translation with the
  line named. `FLAGS` is 64 bits of scratch for the program's own use,
  cleared at start — and per process here, which MMBasic's single
  global could not be.
* ~~**`POS`**~~ — **done**. The column the next character will go in, 1
  at the start of a line, which is MMBasic's convention. The runtime
  had been tracking it all along for `TAB`; `POS` only gives it a name.
* ~~**`FLUSH #n`**~~ — **done**. `fflush` *and* `fsync`, where MMBasic
  has one `f_sync`: the first is enough for another process to see the
  data, the second is what survives the power going off, and a program
  that says `FLUSH` means the second. Channel 0 is the console and does
  nothing, as `cmd_flush` does. Worth knowing before putting one in a
  loop: **Fuzix's `fsync` ignores its fd and syncs the whole
  filesystem** (`Library/libs/fsync.c` is one call to `sync()`), so
  this costs more here than MMBasic's per-file version.
* **`SCHANGE$`**, **`TOPBOTTOM`** (`fun_max_min`) — small string and
  min/max helpers.
* **`LOCATION`** (`cmd_locate`) — cursor positioning; `PRINT @` covers
  the same ground and this is the other spelling.

## Category 2 — real value, moderate work

* **`FRAMEBUFFER LAYER` and `FRAMEBUFFER MERGE`.** The decision is
  already made and written up in the FUZIX tree at
  `platform-rpipico/PC3-LAYER-MERGE.md`: **implement the MERGE model**,
  which is MMBasic's own TFT build, not a compromise invented here — a
  program written for a PicoMite driving an ILI9341 runs unchanged, and
  only the moment of compositing differs. The driver-side `LAYER` model
  is rejected because it needs every buffer readable at scanout rate,
  which is 40K of SRAM this machine does not have. That document
  carries the work sketch (a second PSRAM buffer, a merge routine in
  `display.c`, `GFXIOC_MERGE`, a third `display_fb_select` target, and
  the runtime/translator pair) and it is comparable in size to the
  original FRAMEBUFFER work. **What would change the decision: a mouse
  pointer**, which is the one case where compositing has to be
  continuous.
* **`BLIT`, `BLIT MEMORY`** (`cmd_blit`) — the kernel has `GFXIOC_BLIT`
  already; this is the BASIC surface over it plus the buffer model.
* **`SPRITE` family** and **`TILE`/`TILEMAP`** — large, and they lean on
  BLIT. Worth doing as one piece if games matter.
* **`REDIM [PRESERVE]`** — needs heap-allocated arrays instead of the
  current static ones, which puts `malloc` into generated code that has
  none.
* **The rest of `MATH`** — `M_MULT M_INVERSE M_TRANSPOSE M_DETERMINANT
  V_CROSS V_NORMALISE MAGNITUDE DOTPRODUCT CORREL CHI CROSSING`, plus
  `MATH CRC` and `BASE64`. Pure arithmetic, each small.
* **`ARRAY SLICE`/`ARRAY INSERT`, `MATH C_*`** — index arithmetic.
* **`KEYDOWN`** — needs a key-state table from the kernel; `INKEY$`'s
  one-byte read cannot give it.
* **`GETSCANLINE`** — the kernel knows; a small ioctl.
* **`DRAW3D`** and **`TURTLE`** — whole subsystems in MMBasic. Real
  work, no platform obstacle.
* **`REFRESH`, `RESOLUTION`, `SYNC`, `FRAME`** — display control whose
  meaning has to be decided against this display rather than copied.

## Category 3 — possible, wants your steer first

* **`POKE`, `MEMORY COPY|SET|PACK`** — the pair to `PEEK`. A bad read
  kills one program; a bad write can take the kernel, and there is no
  MMU to disagree.
* **`PEEK(VAR x)`, `VARADDR`, `CFUNADDR`** — ask about a *variable*, so
  they need the symbol table rather than a value.
* **`VAR SAVE`/`RESTORE`/`CLEAR`** (`cmd_var`) — flash-backed variables
  onto a file.
* **`CSUB`** as an `extern` declaration; **`CALL(name$,…)`**;
  **`JSON$`**; **`EVAL`** (which needs the interpreter a translator has
  thrown away, so it is the hardest of these by a distance).
* **`DEVICE`/`DEVICE()`**, **`CONFIGURE`**, **`OPTION`** variants — what
  should they say on a machine that is not a PicoMite?
* **`FLASH`, `RAM`, `DRIVE`** — a storage model that is not this one.
* **`XMODEM`/`YMODEM`** — `uue`/`uud` cover the need already.
* **`ADC` (the command)** — MMBasic's buffered/DMA sampling, as opposed
  to `SETPIN AIN`/`PIN()` which are done. Needs a kernel driver.
* **`SPI2`** is the SD card's controller and **`I2C`** is the QWIIC bus
  the kernel polls the RTC on; both are deliberately not offered today,
  and changing that is an arbitration decision, not a translator one.
* **`TRACE`** — needs a statement-level hook in generated code.

## Category 4 — deliberately out

* **Hardware this machine does not have:** `CAMERA GAMEPAD WII
  WII CLASSIC WII NUNCHUCK KEYPAD KEYBOARD MOUSE HUMID
  DISTANCE PULSIN IR WS2812 STEPPER TMC22XX LCD
  I2CLCD BACKLIGHT GPS TOUCH CLICK CTRLVAL GUI MSGBOX WEB BITSTREAM
  SLEW WATCHDOG`

  `ONEWIRE`, `TEMPR` and `TEMPR START` were on this list and should not
  have been: the machine has one-wire on any header pin, and a DS18B20
  on GP26 is what they were verified against. They are done.
* **PIO, and its assembler.** `PIO` itself plus the mnemonics that only
  exist inside a PIO program: `JMP MOV NOP PUSH PULL WAIT IN OUT SET
  IRQ` and `IRQ CLEAR/NEXT/NOWAIT/PREV/SET/WAIT`. The pin lock refuses
  `PLK_PIO` today pending a survey of which state machines the display
  and sound engines hold.
* **The immediate-mode environment:** `EDIT EDIT FILE LIST NEW RUN
  AUTOSAVE CHAIN LIBRARY EXECUTE HELP CMM2 LOAD CMM2 RUN
  UPDATE FIRMWARE CONFIGURE CPU`. A translated program is compiled and
  run, not typed at a prompt; `mmedit` is the editor.
* **Firmware demos and accelerators:** `MANDELBROT ASTRO STAR RAY CALC
  DRAW3D`(the demo entry points) — a BASIC program can compute these,
  and they exist in the firmware because an interpreter cannot.
* **Interpreter mechanics:** `INTERRUPT IRETURN ONESHOT` — `SETTICK`,
  `ON KEY` and `SETPIN INTx` cover the same ground the way this port
  does it (see `PLAN-interrupts.md`); `REM` is the lexer's; `DEFINEFONT`
  installs into a table that is `const` in flash here.
* **`FM`, `FLAGS`, `PUSH`/`PULL` outside PIO, `NOP`** — no counterpart.

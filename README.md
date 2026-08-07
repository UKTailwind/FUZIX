# mmb2c — MMBasic to C translator

Three files:

| file | what it is |
|---|---|
| `mmb2c.py` | the translator. Runs on CPython **and** on MicroPython. |
| `mmb_runtime.h` / `mmb_runtime.c` | the small C support library the generated code links against. |

```
python3 mmb2c.py prog.bas -o prog.c --report
gcc -std=c99 -I. -o prog prog.c mmb_runtime.c -lm
```

On the Pico:

```python
import mmb2c
mmb2c.convert("prog.bas", "prog.c", True)   # True = print the scope report
```

The source is ~74 KB, so on a Pico compile it with `mpy-cross` first
(`mpy-cross mmb2c.py`) — otherwise the parse alone will eat most of RAM.

---

## Scope model — the implied-global trawl

MMBasic has exactly two scopes and the translator implements both:

* **global** — created by `DIM`, or, far more often, created *implicitly*
  the very first time the name is touched anywhere in the program,
  including inside a `SUB`;
* **local** — a parameter, or a name introduced by `LOCAL` (or `STATIC`)
  inside one `SUB`/`FUNCTION`.

Because implicit creation can happen anywhere, the translator makes three
passes over the source:

1. **declarations** — every `SUB`/`FUNCTION` signature (so calls can be
   resolved before their definition), every `DIM`/`LOCAL`/`STATIC`/`CONST`,
   and every `OPTION`.
2. **scan** — a *full parse* of every statement, resolving each name.
   Anything still unknown at that point is an implied global: it is
   created here, given a type from its suffix or the `OPTION DEFAULT` in
   force, and recorded with the line that created it.
3. **emit** — the same full parse again, now with every name resolved,
   writing C.

Pass 2 is a real parse rather than a keyword trawl, so what it finds is
exactly the set of names pass 3 will emit — the two cannot disagree.

The report (`--report`, and also written as a comment block at the top of
the generated `.c`) has two sections. The second one is the useful one:

```
Globals reached from inside a SUB or FUNCTION.
Anything marked "implied" was never DIMmed and is
shared with the whole program - check that a LOCAL
was not what you meant:
    SUB      MakeMess (line 71):
        hidden.counter       FLOAT    implied  used line 73
        oops                 FLOAT    implied  used line 72
```

`OPTION EXPLICIT` turns every implied global into a hard error, and
`OPTION DEFAULT NONE` into "no type" errors, so an existing program can be
tightened up incrementally.

Note one deliberate difference from the interpreter: `LOCAL` is treated as
applying to the whole routine, not from the point it executes. Referencing
a name *earlier* in a routine than its `LOCAL` statement resolves to the
local here, where MMBasic would hit the global.

---

## Strings

Exactly MMBasic's layout, so `CHR$(0)` inside a string is not a problem:

```
s[0]      length, 0..255
s[1..len] the characters
s[len+1]  a NUL the runtime maintains, not part of the string
```

A string variable is `char x[MM_STRSZ]` (257 bytes). Scalars live in
static storage; global arrays and strings live in one `mm_heap` block
(PSRAM on the PC3) allocated once at startup, and a `LOCAL` array gets
`mm_lheap`/`mm_lfree` per invocation — so generated code still contains
no `malloc` of its own. String literals compile to their own length byte:
`"Hello"` becomes `"\005" "hello"` — two adjacent literals so the length
byte can never be swallowed by a following hex digit.

Expression temporaries come from a fixed stack of `MM_TMPN` (default 16)
scratch buffers. Every generated function takes a mark on entry, every
statement winds back to it, and every loop test winds back before it is
re-evaluated — so a `DO ... LOOP` doing string work runs forever without
growing. Exhausting the stack raises a runtime error rather than silently
corrupting a buffer. Raise `MM_TMPN` if you hit it.

---

## Argument passing

MMBasic passes simple variables by reference unless `BYVAL` is used, and
the generated C does the same:

* numeric by-ref parameter → `MMFLOAT *p_x`, referenced as `(*p_x)`;
  the call site passes `&v_a` for a bare variable, or a C99 compound
  literal `(MMFLOAT[]){ expr }` for anything else;
* string parameter → `char *`, which is by reference already; `BYVAL`
  wraps it in `mm_scopy()`;
* whole array `a()` → decays to a pointer (one dimension only for now);
* omitted (`Foo 1, , 3`) and missing trailing arguments are padded with
  zero / `""`, as MMBasic does.

---

## Numbers in, numbers out

`PRINT`, `STR$()` and `FORMAT$()` do not use C's `printf`. The
interpreter's own `IntToStr` / `IntToStrPad` / `FloatToStr` (from
`core/MMBasic.c`) are ported into `mmb_runtime.c` verbatim, so the digits
match: 9 significant figures, automatic trailing-zero trimming, and
scientific notation below 0.0001 or at/above 1000000. `tests/xcheck`
compiles the interpreter's functions and this runtime's side by side and
compares them over 4896 combinations of value, width, precision and pad
character — currently zero mismatches.

`PRINT`'s column tracking is the interpreter's too: 1-based, with a comma
advancing to the next 14-column tab stop, which is what `TAB()` needs to
land in the right place. `TAB()` is a string-valued function here exactly
as it is in MMBasic, not a side effect of `PRINT`.

---

## Currently translated

`OPTION DEFAULT|EXPLICIT|BASE` · `DIM` / `LOCAL` / `STATIC` / `CONST`
(type prefix, `AS type`, suffixes, initialisers, arrays, 1-D initialiser
lists) · assignment with or without `LET` · `MID$(...)=` ·
`PRINT` with `;` `,` and `TAB()` · `IF/THEN/ELSEIF/ELSE/ENDIF`, block and
single-line · `FOR/NEXT/STEP` (limit evaluated once, runtime-signed step)
· `DO/LOOP [WHILE|UNTIL]` · `WHILE/WEND` · `SELECT CASE` with value,
`a TO b` and `IS <op>` tests · `EXIT FOR|DO|SUB|FUNCTION` · `GOTO` and
labels · `END` · `SUB`/`FUNCTION` including recursion and string returns
· `RANDOMIZE` · file handling (see below) · `DATA`/`READ`/`RESTORE` ·
`SORT` · `CONTINUE FOR|DO` · `INC` · `CAT` · `ON n GOTO|GOSUB` ·
`GOSUB`/`RETURN` · `ERROR` · `PAUSE` · `TIMER =` / `DATE$ =` / `TIME$ =`
· `ARRAY SET|ADD` · `MATH SET|SCALE|ADD|RANDOMIZE` · `ERASE` / `CLEAR` ·
the `LONGSTRING` family.

On the PC3 (where the kernel owns the display, sound and GPIO) also:
graphics — `MODE` · `CLS` · `COLOUR` · `PIXEL` (scalar and array forms,
and the function) · `LINE` · `CIRCLE` · `TEXT` · `FONT` ·
`MAP` (`SET|RESET|MAXIMITE|GRAYSCALE`, `MAP(n)=` and the function) ·
`FRAMEBUFFER CREATE|CLOSE|WRITE|COPY|WAIT` · `PRINT @(x,y)` ·
`MM.HRES` / `MM.VRES`; GPIO — `SETPIN pin, DIN|DOUT` · `PIN(n)=` ·
`PIN(n)` (GPIO numbering, not connector pins); sound — `PLAY MP3` ·
`PLAY VOLUME` · `PLAY STOP` (the decoder is a separate spawned process);
and the spawns — `SYSTEM prog$[, args]` · `SAVE IMAGE` · `LOAD IMAGE`.
On a host with no display the graphics calls are silently nothing, which
is what keeps the test gates meaningful.  `CIRCLE`, `TEXT` and the `MAP`
palettes are static functions in per-feature headers, so a program pays
only for the primitives it uses; the rest cross into the kernel through
bcrun.

Operators follow the manual's precedence table, including `\`, `MOD`, `^`,
`<<`, `>>`, bitwise `AND`/`OR`/`XOR`, logical `NOT`, bitwise `INV`, and
integer-vs-float promotion (`/` always produces a float).

### Built-in functions

| group | functions |
|---|---|
| arithmetic | `ABS INT FIX CINT SGN MAX MIN RND PI` |
| trig / logs | `SIN COS TAN ATN ASIN ACOS ATAN2 DEG RAD SQR LOG EXP` |
| `MATH(...)` | the scalar members: `COSH SINH TANH LOG10 ATAN3` |
| bits | `BIT` and the `AND OR XOR INV << >>` operators |
| arrays | `BOUND` (including on an array passed to a SUB) |
| strings | `LEN ASC BYTE CHR$ LEFT$ RIGHT$ MID$ INSTR UCASE$ LCASE$ SPACE$ STRING$ LTRIM$ RTRIM$ TRIM$ FIELD$ VAL` |
| conversion | `STR$` (all four arguments) `FORMAT$ HEX$ OCT$ BIN$ BIN2STR$ STR2BIN` |
| date / time | `DATE$ TIME$ DATETIME$ DAY$ EPOCH TIMER` (all accept `NOW`) |
| files | `EOF LOC LOF INPUT$ DIR$ CWD$` |
| array stats | `MATH(SUM/MEAN/SD/MAX/MIN/MEDIAN a())`, with `MAX`/`MIN` writing back the optional index |
| long strings | `LLEN LGETSTR$ LGETBYTE LINSTR LCOMPARE LINPUT` |
| console / graphics | `INKEY$` (non-blocking, MMBasic key codes) `PIXEL(x,y) MAP(n) PIN(n) MM.HRES MM.VRES` |
| misc | `CHOICE TAB RGB` (including the colour-name shortcuts) |

A `SUB` or `FUNCTION` you define always wins over a built-in of the same
name — the manual's own worked example defines `Trim$()`, and a program
written before a built-in existed has to keep working. You get a warning
when that happens. Declaring a *variable* with a built-in's name is an
error, as it is in the interpreter.

One deliberate difference from the manual: `FIELD$(s$, 2, ",", "'")` keeps
the quote characters in the returned field. The manual's example shows
them stripped, but `fun_field()` in the firmware copies them, and this
follows the code rather than the prose.

---

## Files

MMBasic channels 1..10 map straight onto stdio `FILE*`, and channel 0 is
the console — so one set of PRINT/INPUT routines serves both, exactly as
`#0` does in the interpreter.

```basic
OPEN f$ FOR INPUT|OUTPUT|APPEND|RANDOM AS [#]n   ' -> fopen rb/wb/ab/r+b
CLOSE [#]n [, [#]n ...]                          ' -> fclose
PRINT [#n,] ...                                  ' same formatting as the console
INPUT [#n,] a, b$, c                             ' comma separated, quotes honoured
INPUT "prompt"; a          ' ';' adds the "? ",  ',' suppresses it
LINE INPUT [#n,] s$        ' whole line; CR dropped, LF ends it
SEEK #n, pos               ' 1 based, as MMBasic counts
```

Functions: `EOF(#n) LOC(#n) LOF(#n) INPUT$(count, #n) DIR$(spec[, ALL|DIR|FILE]) CWD$`.
Management: `KILL RENAME ... AS ... COPY ... TO ... MKDIR RMDIR CHDIR FILES`.

Details worth knowing:

* `EOF()` peeks. C's `feof()` only latches *after* a read has already
  failed, so `DO WHILE NOT EOF(#1)` would run one iteration too many if it
  were used directly. The runtime does `fgetc`/`ungetc` instead.
* `LOC()` and `SEEK` are 1-based, matching `fun_loc()` (`ftell + 1`) and
  `cmd_seek()` (`getint(...) - 1`) in the firmware.
* `RANDOM` opens `r+b`, creates the file if it is missing, and positions
  at the end, as the manual specifies.
* Channels are closed on `END`, on a runtime error, and when `main()`
  returns, so output is always flushed.
* `INPUT`'s field splitting follows `cmd_input()`: leading spaces skipped,
  a quoted field runs to the closing quote and then to the next comma,
  an unquoted field runs to the comma with trailing spaces trimmed.
  Numeric fields go through `strtoll`/`atof`, so `&H` prefixes are *not*
  honoured there — only `VAL()` does that, same as the interpreter.

Everything above is ISO C except the six calls that need a directory
layer — `MKDIR RMDIR CHDIR DIR$ FILES CWD$`. Those have two ports:
`dirent.h` / `sys/stat.h` / `unistd.h` on POSIX, and `direct.h` /
`io.h` with `_findfirst` on Windows, so MSVC and MinGW build without a
shim. Wildcard matching is done in the runtime rather than handed to the
platform, so both ports agree with each other and with MMBasic. Build
with `-DMM_NO_DIRS` on a target with no filesystem at all and those six
become clean runtime errors; the rest of the file handling is untouched.

Serial ports (`OPEN comspec$ AS #n`) are rejected with a message rather
than silently mistranslated.

---

## DATA, SORT and the other Tier A statements

**`DATA` / `READ` / `RESTORE`.** Every `DATA` item is gathered in the
declaration pass and compiled into one static table. MMBasic keeps the
raw *text* of an item and converts it when it is `READ`, so each entry
carries both forms — which is why `DATA 10` can be read into a string as
`"10"`, and why the manual's unquoted-string form (`DATA alpha, beta`)
works. `RESTORE label` resolves at compile time to that label's index in
the table. `READ a()` fills a whole array, and `READ SAVE` / `READ
RESTORE` nest up to eight deep.

**`SORT array() [,index()] [,flags] [,start] [,count]`.** A shell sort,
in place, moving the index array in step — no scratch memory and no
recursion, both of which matter on a Pico. The documented flag bits are
all honoured: bit0 reverse, bit1 case independent, bit2 empty strings to
the end. The index array comes back holding each element's position
before the sort, so parallel arrays can be reordered to match.

**`MATH(...)` array reductions.** `SUM MEAN SD MAX MIN MEDIAN`. `SD` is
the sample form, `sqrt(var / (n-1))`, matching `MATHS.c`. `MEDIAN` uses
selection rather than sorting a copy, so it never disturbs your array and
never allocates — the cost is O(n^2), which is the right trade for the
array sizes a Pico deals in.

**`CONTINUE FOR` / `CONTINUE DO`** become C `continue`, which lands
correctly in every loop form: in a `DO…LOOP UNTIL` (a C `do{}while`) it
jumps to the test, which is exactly "skip to the end, then test".

**`ERASE` and `CLEAR`** zero their variables rather than freeing them —
this translator's storage is static, so there is nothing to hand back.
You get a warning saying so rather than a silent difference.

**`ON ERROR`** accepts `ABORT` and `CLEAR` (the default behaviour) and
rejects `IGNORE`, `SKIP` and `RESTART` with a clear message, because soft
error recovery needs a `setjmp` pass that is not in yet.

A label is only emitted into the C when something actually jumps to it,
and the "label inside a block" warning only fires when a `GOTO` from
*outside* that block targets it — so the common pattern of `ON n GOTO`
into labels within the same loop is silent.

---

## Long strings, and GOSUB

**`LONGSTRING`.** A long string is an `INTEGER` array using the
firmware's own layout — the byte count in element 0, the raw payload
(not NUL terminated) from element 1 on, so `DIM b%(n)` holds `n*8` bytes.
That is stated outright in `misc/Custom.c`, where `JSON$` reads a long
string, and it is what this implementation follows.

Commands: `CLEAR APPEND LOAD COPY CONCAT LEFT RIGHT MID REPLACE RESIZE
TRIM SETBYTE UCASE LCASE PRINT` (with an optional `#n` and trailing `;`).
Functions: `LLEN LGETSTR$ LGETBYTE LINSTR LCOMPARE LINPUT`.

Two details from the manual that are easy to get backwards, and which
`tests/t8.bas` checks: `LGETBYTE` and `LONGSTRING SETBYTE` index bytes
according to `OPTION BASE`, while `LINSTR` and `LGETSTR$` are always
1-based whatever the base is.

One deliberate improvement: the manual says overflowing a long string is
"undefined". Here it is a clean runtime error naming the operation, since
silently scribbling past an array is the worst possible outcome on a
microcontroller.

**`GOSUB` / `RETURN`** — legacy, and the generated C is not pretty, but
it is correct. Each `GOSUB` becomes `mm_gosub_push(id); goto L_target;`
followed by its own return label, and each `RETURN` becomes a `switch` on
the popped id that jumps back to the right one. Nesting works (50 deep,
as MMBasic allows), as does `GOSUB` from inside a loop or an `IF`, and
`ON n GOSUB`.

The one thing it cannot do is jump between C functions, so a `GOSUB`
inside a `SUB` targeting a label in the main line is refused with a
message rather than mistranslated. A `GOSUB` wholly inside one `SUB` is
fine. The manual calls `GOSUB` legacy and recommends `SUB` for new code,
which remains good advice.

---

## IF and statement separators

All of these are handled, and `tests/t3.bas` / `tests/t4.bas` exercise
every one of them:

```basic
IF x THEN stmt                       ' single line, no ELSE
IF x THEN stmt ELSE stmt
IF x THEN stmt : stmt : stmt         ' ALL of them are conditional
IF x THEN s1 : s2 ELSE s3 : s4       ' the ELSE takes the rest of the line
IF x THEN GOTO label                 ' and  IF x GOTO label
IF x THEN 200                        ' a line number is a GOTO target
IF x THEN IF y THEN s ELSE s         ' ELSE binds to the innermost IF
IF x THEN                            ' block form; a trailing comment
  ...                                '   still counts as "nothing follows"
ELSEIF y THEN
  ...
ELSE
  ...
ENDIF                                ' or END IF
```

Statements separate with `:` anywhere — `a=1:b=2:PRINT a`, empty
statements (`::`), a trailing colon, `label: stmt : stmt`, and an `IF`
as the tail of a colon list. A parameterless subroutine call at the
start of a line followed by a colon (`Ping : Ping`) is *not* mistaken
for a label: routine names are collected in a pre-scan before labels are
resolved.

Two constructs are rejected rather than mistranslated: a single-line
`IF` that tries to open a multi-line block, and a numeric `GOTO` target
that does not exist. A label placed *inside* a block gets a warning,
since jumping to it from outside skips the block's set-up (`FOR` limits
and the like) — the interpreter tolerates this, C does not.

---

## When something cannot be translated

By default an untranslatable statement does not stop the conversion. It is
rolled back, left in the C as a comment naming the line and the reason, and
listed in the report:

```
/* MMBASIC line 949 not translated: array rank mismatch */
/*     matxvec(tmatrix(), rhohatijk(), rhohatsez()) */
```

Nothing is emitted in its place, so check the surrounding logic still makes
sense — particularly if the skipped line opened a block. Pass `--strict` to
stop on the first one instead.

A 3213-line real program (a solar eclipse predictor for the Micromite
eXtreme) currently converts with **zero** skipped lines, compiles clean, and
reproduces the August 2017 eclipse to the second.

---

## Not yet

Statements: `TYPE` structures, `REDIM`, `OPTION ESCAPE` backslash
escapes, multi-dimensional array parameters, `ON ERROR IGNORE|SKIP`, and
the `LONGSTRING AES128` / `BASE64` members.

Functions that need the Pico itself or the interpreter's own machinery,
and which a plain C translation cannot honestly provide: `PORT
PULSIN DISTANCE TEMPR SPI PIO DEVICE TOUCH CLICK SPRITE GETSCANLINE
KEYDOWN GPS DRAW3D` (hardware), `JSON$` and the wildcard/bulk forms
of `KILL` and `COPY`, `EVAL CALL PEEK STRUCT` (interpreter internals), and the array, matrix, complex-number, CRC and PID members of `MATH(...)`.
(`PIN`, `PIXEL`, `MAP` and `INKEY$` used to be in this list; on the PC3
they are translated — see "Currently translated" above.)

`COVERAGE.md` triages every remaining keyword in the manual. Anything not
recognised produces an error naming the line and the construct, rather
than silently wrong C.

---

## Building

On Windows there is `build.bat` instead:

```
build tests\t1.bas          translate and compile with cl or gcc
build tests\t1.bas run      ... and run it
```

On anything with GNU make:

```
make            translate and build every tests/*.bas into build/
make run        ... and run them
make xcheck     compare the number formatting against the interpreter's own
make clean
```

---

## Licence

MMBasic licence (Graham / Mather BSD-4-clause) — see `LICENSE`.

`mmb_runtime.c` contains `IntToStr()`, `IntToStrPad()` and `FloatToStr()`
ported essentially unchanged from `core/MMBasic.c` in the
[PicoMite firmware](https://github.com/UKTailwind/PicoMite), which is why
`PRINT`, `STR$()` and `FORMAT$()` agree with the interpreter to the digit.
Those functions carry the copyright above and the notice travels with them.

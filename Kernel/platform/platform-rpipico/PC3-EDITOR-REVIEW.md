# Porting the MMBasic editor to Fuzix: a review

Question: can MMBasic's full-screen editor become a Fuzix file editor,
with BASIC syntax colouring, running as an ordinary task with the file
buffered in the PSRAM arena?

Answer: yes, and it is a much smaller job than the 9,076 lines of
Editor.c suggest.  Two things in the kernel console must be fixed
first, and there is one performance unknown worth measuring before
committing to the design.

## What the machine has today

There is **no editor at all** on the card - `ls /usr/bin` has no `ed`,
no `vi`, no `ue`, no `levee`.  Files are written on a PC and sent over
with `uusend.py`.  So this is not a matter of preferring a nicer
editor to an existing one; it is the missing piece that stops the
machine being self-contained, in the same way the compiler was before
v0.4.

Relevant facts, all verified on the board:

- The tty has full termios: `icanon`, `echo` and `isig` can all be
  turned off, so raw-mode input works.
- The tty already reports **40 rows, 80 columns**, so the editor can
  take its geometry from `TIOCGWINSZ` rather than a compiled-in guess.
- A process gets `PROGSIZE` = 256K minus udata.
- The PSRAM arena gives 8 slots per process, 4K granular, as a **raw
  base address usable as a plain C pointer**.  It is not swapped, not
  context-switch copied, and released on exec and exit.

## What we are actually taking

Editor.c is 9,076 lines, but the file manager is more than half of it
and is out of scope ("file edit only"):

    lines      what                                    take?
    75-888     display plumbing + edit() entry points   yes    814
    889-6105   FILE MANAGER (fm_*)                      no   5,217
    6106-7411  FullScreenEditor - main loop, keys       yes  1,306
    7412-8302  MarkMode - block mark and clipboard      yes    891
    8303-8641  SetColour - syntax colouring             yes    339
    8642-9076  printLine/printScreen/insert/status      yes    435

**~3,785 lines to port**, and a good part of the 814 is MMBasic
display plumbing that gets replaced rather than translated.

Its dependencies on MMBasic internals are remarkably light - the whole
file references only:

    MMInkey 17   error 15   routinechecks 9   GetMemory 9
    ProgMemory 7   MMgetchar 4   ClearVars 4   tokenise 3
    commandtbl 3   SaveContext 2   RestoreContext 2   tokentbl 1

and of those, `ProgMemory`, `SaveContext`, `RestoreContext`,
`ClearVars`, `IfTableFree` and `SaveToProgMemory` exist purely to
support editing the in-memory program - which file-only editing does
not need.  What is left to provide is `MMInkey`, `MMgetchar`,
`GetMemory`, `error`, `routinechecks`, and the keyword tables.

## The big thing in our favour: the VT100 backend already exists

The editor was written with two output backends.  Alongside the
MMBasic display path (`MX470PutS`, `DrawBox`, tile colours) there is a
serial-terminal path: `PrintString`/`SSputchar` are function pointers
that default to `SerialConsolePutC`, and the colouring function is
literally `SetColour(unsigned char *p, int DoVT100)`.

The sequences it emits are ordinary ANSI:

    SGR 30-37, 40-47, 0, 7, 4      colours, inverse, underline
    CSI K, CSI J, CSI 2J CSI H     erase line / display / home
    CSI ?25 l/h                    cursor hide/show
    ESC 7 / ESC 8                  save/restore cursor
    CSI ?7 l/h                     autowrap off/on
    CSI ?1000 l/h                  mouse reporting

Our console.c implements a VT100/CSI subset already, and it covers
almost all of that: SGR 0,1,7,22,27,30-37,39,40-47,49,90-97,100-107,
ESC 7/8, CSI A-H, J, K, m, s, u, ?25 l/h.  Mouse reporting we neither
have nor want, and unknown sequences are parsed and swallowed rather
than printed, so it does no harm.  SGR 4 (underline) is accepted but
not rendered - cosmetic.

**We do not have to write a renderer.**  That is the single reason
this port is a few weeks rather than a rewrite.

## Blocker 1: the console always wraps

`charout()` in console.c wraps unconditionally the moment the 80th
column is written:

    if (++cx >= CON_COLS) { cx = 0; cy++; }

There is no DECAWM, and no deferred wrap.  A full-screen editor cannot
live with this: painting the last column of any row moves the cursor
to the next line, and painting the bottom-right cell scrolls the
entire screen.  The editor sends `CSI ?7 l` precisely to prevent it,
and console.c swallows it.

Two pieces of work, both in console.c:

1. Implement `CSI ?7 h/l` and honour it.
2. Implement the **deferred wrap** VT100 actually specifies: writing
   the last column leaves the cursor *on* that column with a pending
   flag; the wrap happens when the next printable character arrives.
   This is worth doing regardless of the editor - it is why a
   full-width line renders correctly on a real terminal, and any
   full-screen program we ever run will assume it.

This is the one change that must land in the kernel, and it is small
and independently testable.

## Blocker 2: no function keys

The editor is driven from the function keys - F1 save-and-exit, F2
save-exit-and-run, F3 find, F5, F10, and so on, plus SHIFT-F3 for
find-again.  All twelve appear in the key dispatch.

`kbd_decode.c` emits proper VT100 for arrows, Home, End, Insert,
Delete, PgUp and PgDn - but HID usages 0x3A-0x45 (F1-F12) have **no
cases at all**, so the USB keyboard produces nothing for them.  They
need adding as the usual xterm sequences (`ESC O P..S` for F1-F4,
`ESC [ 15~` onwards for F5-F12), with shifted variants.

## The shim we have to write

Small, and the only genuinely new code:

- **`inkey()`** - read raw bytes and reassemble CSI/SS3 sequences back
  into the editor's single key codes (`F1`, `UP`, `PUP`, ...).  This
  is the inverse of what kbd_decode.c does, and both ends must agree.
  ~150 lines.
- **raw mode** - `tcsetattr` with `icanon`, `echo` and `isig` off,
  restored on exit and on signal.  The editor must not be killable
  into a wedged terminal.
- **`GetMemory()`** -> `PSRAMIOC_ALLOC`.
- **`error()`** -> print and return to the shell.
- **`routinechecks()`** -> no-op, or a signal check.
- file load/save, and `main()` in place of `cmd_editfile()`.

## Memory: the file in PSRAM

The editor keeps the whole file in one contiguous buffer and memmoves
the tail on every insert and delete.  The arena suits that exactly: a
single allocation, a raw pointer, no swap interaction.  Where MMBasic
squeezes into a ~96 KB `EDIT_BUFFER_SIZE`, we can hand it a megabyte
and stop thinking about it.

**The one thing to measure before committing**: every keystroke at the
top of a large file memmoves the whole tail *through PSRAM*.  MMBasic
gets away with this because its buffer is 96 KB of SRAM.  A 1 MB file
in PSRAM is a different proposition, and I do not yet know the
achievable memmove bandwidth on this part.

Measure it first.  If a worst-case keystroke costs more than about
20 ms the editor will feel sticky, and the fix is a **gap buffer** -
keep the free space at the cursor so ordinary typing moves nothing,
and only pay when the cursor jumps.  That is a contained change to
`editInsertChar` and `findLine`, but it does break the editor's
assumption that the buffer is flat, so it is much cheaper to decide
before porting than after.

A cheaper hedge, if the measurement is bad: cap the file size at
something like 128 KB and keep the buffer in the process's own SRAM,
using PSRAM only for the undo/clipboard. Less ambitious, no gap
buffer, and still far beyond what the machine can do today.

## Syntax colouring: a decision to make

`SetColour` recognises keywords by looking them up in MMBasic's
`commandtbl` and `tokentbl`, plus three of its own lists
(`twokeywordtbl`, `specialkeywords`, `hiddenfunctions`).  A standalone
editor has none of those tables, so we must supply a keyword list.
Two options, and they give genuinely different products:

1. **Every MMBasic keyword.**  Extract the command and token names
   from MMBasic's headers into a static table - a few hundred strings,
   generated, not hand-typed.  Faithful to MMBasic, and the right
   answer if the editor is for writing MMBasic generally.
2. **Only what `mmbc` supports.**  We already generate exactly this
   list for Appendix C of the manual, from mmb2c's own dispatch and
   builtin tables (59 statements, 69 functions, `fcc/coverage.py`).
   Colouring only the supported set turns the editor into live
   feedback on what will actually translate - type `SPRITE` and it
   stays white.

My recommendation is (1) with (2) available as a switch, because a
keyword that is real MMBasic but unsupported here should not look like
a typo - but it *should* be distinguishable, so a third colour for
"valid MMBasic, not supported by mmbc" may be better than either.
Your call; it changes the table format, so it is worth settling first.

## Proposed staging

1. **Kernel console: deferred wrap + DECAWM, and F1-F12 in
   kbd_decode.c.**  Independent of everything else, testable on its
   own, and useful to any full-screen program.
2. **Measure PSRAM memmove bandwidth** and decide flat buffer vs gap
   buffer.
3. **The shim**: raw mode, `inkey()`, arena allocation, file I/O,
   stubs.  Get a stub editor that paints a file and moves the cursor.
4. **Import the editor core** with the file-manager and
   program-memory paths removed and the VT100 backend forced on.
5. **Colouring**: the keyword table, per the decision above.
6. **Test**, and add it to the manual.

Stage 1 is worth doing whatever happens to the rest - it is a real
defect in the console today.

## Licensing

Editor.c carries MMBasic's BSD-style header and you are one of the
copyright holders, so this is your call rather than a question.  Worth
recording only because it lives in our fork: FUZIX upstream takes no
AI-generated code, so this is not an upstreaming candidate in any
case.

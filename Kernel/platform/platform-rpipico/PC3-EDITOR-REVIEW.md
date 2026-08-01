# Porting the MMBasic editor to Fuzix: a review

Question: can MMBasic's full-screen editor become a Fuzix file editor,
with BASIC syntax colouring, running as an ordinary task with the file
buffered in the PSRAM arena?

Answer: yes, and it is a much smaller job than the 9,076 lines of
Editor.c suggest.  Two things in the kernel console must be fixed
first, and there is one performance unknown worth measuring before
committing to the design.

## What the machine has today

`/bin` has `ed`, `vi`, `vile`, `levee` and `ue`.  What it does not have
is anything modern or WYSIWYG, and `ue` in particular mangled the
display - which turned out to be the console's wrap bug, not `ue`
(see STAGE 1 below).  None of them colour BASIC.

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

## STAGE 1 IS DONE (commit 41d7ee07b)

Both blockers below are fixed and measured on hardware.  `wraptest`
(utils/wraptest.c) asks the console where the cursor actually is with
CSI 6n, so the result does not depend on anyone looking at the screen.

Against the old kernel, 4 of 7 failed - including the one that matters
most, writing the bottom right cell scrolling the whole screen:

    after writing col 80, cursor stays    (2,1)  want (1,80)  FAIL
    DECAWM off: no wrap, stays on col 80  (7,2)  want (6,80)  FAIL
    DECAWM back on: defers again          (9,1)  want (8,80)  FAIL
    bottom-right cell does not scroll     (40,1) want (40,80) FAIL

Against the new kernel all seven pass.  The rest of this section is
kept as the record of what was wrong and why.

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

## Blocker 2: the function keys were not VT100

The editor is driven from the function keys - F1 save-and-exit, F2
save-exit-and-run, F3 find, F5, F10, and so on, plus SHIFT-F3 for
find-again.  All twelve appear in the key dispatch.

The keyboard was **inconsistent with itself**.  `kbd_decode.c` emits
proper VT100 for arrows, Home, End, Insert, Delete, PgUp and PgDn, but
F1-F12 fell through to the layout tables, which carry MMBasic's
pseudo-ASCII codes (0x91-0x9c, shifted 0xb1-0xbc) because they were
imported from MMBasic wholesale.  So F1 pushed a bare 0x91 while the
up arrow pushed `ESC [ A`.

Fixed by emitting what a terminal emits - SS3 P..S for F1-F4,
`CSI 15/17..24 ~` for F5-F12 (16 and 22 skipped, as on a VT220), and
xterm's modifier form when shifted.  The deciding argument is that one
`pc3` termcap entry has to describe both this keyboard and a serial
terminal on the same tty, and the editor must work over the serial
console - TeraTerm cannot send 0x91.  A program wanting MMBasic's
codes maps them back in its own `inkey()`, which the editor has to do
for the serial case anyway.

termcap gained `xn` (the magic-margin flag, which now describes us
honestly), `k1`-`k12`, and the editing cluster - so `vi`, `levee` and
`ue` get function keys as a side effect.

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

### MEASURED (psbench, on the board)

    PSRAM  memmove(p+1, p, n)   12 MB/s   (any size, any alignment)
    PSRAM  memmove(p+4096, p, n) 12 MB/s
    SRAM   memmove(p+1, p, n)   44 MB/s
    sustained: 200 inserts x 64K tail = 1066 ms, 5330 us each

PSRAM is 3.7x slower than SRAM, and the editor's awkward case - an
overlapping backward move at a one-byte offset - costs no more than an
aligned one, so there is no fast path being missed.  Cost of one
keystroke, which is one memmove of everything after the cursor:

    tail     16K    32K    64K    128K    256K    512K
    PSRAM   1.3ms  2.5ms  5.4ms  10.7ms  21.5ms  42.9ms

**Second constraint, and it is the binding one: the arena is 1024 KB
in total** ("PSRAM disc 7104KiB (arena 1024KiB)"), and cc2 wants it too
for ARENA_TABLES.  A megabyte edit buffer would take the whole thing.

### DECISION: flat buffer, capped at 128 KB

Worst case - typing at the very top of a full 128 KB file - is 10.7 ms
per keystroke, and the typical case (cursor mid-file) is half that.
Usable.  128 KB is about 4,000 lines of BASIC, larger than the solar
eclipse, and comfortably more than MMBasic's own 96 KB editor buffer,
so we are running the reference implementation inside the size it was
designed for.

The decisive argument is not the milliseconds, it is that a flat
buffer means **MMBasic's editor code is used unmodified**.  In a
3,785-line port every deviation is a bug I would have to find on
hardware.  A gap buffer - free space parked at the cursor, so ordinary
typing moves nothing - is the textbook answer and would make file size
a non-issue, but it breaks the assumption that the buffer is flat, and
that assumption is threaded through findLine, printLine, SetColour and
the mark/clipboard code.

So: flat now, and if it proves sticky in real use, add the gap buffer
knowing it is needed rather than guessing.  Holding a key down is the
case to watch - auto-repeat at 30/s against a 64 KB tail is 5.3 ms per
repeat, which is 16% of the CPU doing nothing but memmove.

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

**DECIDED: three colours.**  A keyword that is real MMBasic but not
supported by mmbc gets its own colour - distinct from both a supported
keyword and from ordinary text.  That makes the editor live feedback
on translator coverage without ever lying about what is valid MMBasic.
The table therefore needs a flag per entry, not two separate lists:
generate it from MMBasic's command/token names with the mmbc-supported
set (fcc/coverage.py) marking the flag.

## Proposed staging

1. **DONE** - Kernel console: deferred wrap + DECAWM, F1-F12 in
   kbd_decode.c, termcap.  `wraptest` passes 7/7 on hardware.
2. **DONE** - PSRAM measured at 12 MB/s (psbench).  Flat buffer,
   capped at 128 KB; gap buffer only if it proves sticky in use.
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

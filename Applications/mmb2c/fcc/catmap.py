"""Which of the five categories each unimplemented name belongs to.

Hand-kept, deliberately: a scan can say whether the translator
dispatches a name, and cannot say whether the gap is worth closing.
The five are COVERAGE.md's four with one added for this machine:

  1  Finish what is already there
  2  Real value, moderate work
  3  Possible, wants your steer first
  4  Deliberately out
  5  Not applicable to this machine

FN marks a FALSE NEGATIVE - the thing works and the scanner cannot see
it, because the scanner reads statement_inner's dispatch and these are
handled by the lexer, the expression parser, or a two-word branch.
"""

CAT = {
    # ---- FALSE NEGATIVES: implemented, invisible to the scan ---------
    # Operators, parsed by the expression grammar and never dispatched.
    "+": "FN", "-": "FN", "^": "FN", "*": "FN", "/": "FN", "\\": "FN",
    # AllCommands.h writes the integer-divide operator as an escaped
    # backslash, so the scan captures TWO characters, not one.
    "\\\\": "FN",
    "<<": "FN", ">>": "FN", "<>": "FN", ">=": "FN", "<=": "FN",
    "<": "FN", ">": "FN", "=": "FN",
    # Comments, taken by the lexer before a statement exists.
    "Rem": "FN", "/*": "FN", "*/": "FN",
    # Dispatched by do_blit_memform - BLIT and SPRITE share it.
    "Blit Memory": "FN",
    # PRINT @(x, y [, mode]) - MMBasic's fun_at, and there is no other
    # form of it: fun_at errors unless the enclosing command is PRINT.
    # do_print handles it, mode argument and all, so the scan of
    # statement_inner and BUILTINS cannot see it.
    "@(": "FN",

    # ---- 1: finish what is already there ----------------------------
    # EMPTY, and that is not an accident: the category held the members
    # missing from families that were otherwise present, and all of them
    # have been done - ARRAY SLICE, ARRAY INSERT and COLOUR MAP (with
    # MATH SLICE and MATH INSERT, which MMBasic implements with the very
    # same two functions).  Anything that lands here again should be
    # small and should be finished rather than queued.
    #
    # If a name reappears below as UNCLASSIFIED, the scan has stopped
    # seeing something that works.  That is the alarm, not a bug.

    # ---- 2: real value, moderate work ------------------------------
    # ADC: continuous sampling.  It sat above this divider for a while,
    # inside category 1's comment block, reading as an unfinished
    # leftover in a category the document declares empty.  It is not
    # one: the kernel has no ADC path at all (the ADVAL selectors were
    # removed, misc.c:885-913) and MMBasic DMAs the FIFO straight into
    # the BASIC array, which this machine's DMA law forbids - so it is
    # a kernel driver with its own .bss buffer, and the largest thing
    # in this category.
    "ADC": 2,
    # Peripherals: a pin here is a register access, not a syscall, so
    # each is a small userland driver (PC3-IO-PLAN.md).
    # NOTE (reviewed 2026-08-22, reading the reference's actual loops):
    # NONE of these peripherals masks interrupts in MMBasic.  Humid's
    # DHmem is an unmasked us-clock poll whose checksum catches an
    # IRQ-corrupted read (External.c:4033); IR send is an unmasked 13us
    # toggle loop (2715); Pulsin/Distance are unmasked busy-waits.  So
    # all are plain userland work here - registers plus pc3_us64() -
    # and cheaper than this table once assumed.
    # ... but "does it mask?" was the wrong question for THIS machine,
    # and the 2026-08-23 audit corrected it: the 5ms system tick holds
    # di() across its whole body (devices.c:83-139) whatever else is
    # running, and any userland spin over 5ms has the USB host pump
    # injected into it (devices.c:123-133).  So an unmasked us-loop is
    # not automatically safe here either.  Humid survives on its
    # checksum, IR SEND can go through the shipped BITSTREAM PIO path
    # instead of a loop, and Pulsin/Distance want the interruption
    # budget measured on the board before they are promised.
    #
    # OneShot and IR RECEIVE were not part of that review at all: both
    # are GPIO edge interrupts with their own timers in the reference
    # (External.c:286-345, 6106-6188), not userland register work.
    "IR": 2, "Humid": 2, "Distance(": 2, "Pulsin(": 2,
    "OneShot": 2, "SYNC": 2, "Slew": 2, "Keypad": 2,
    # Json$(, WatchDog and CPU were category 2 until the WEB campaign
    # delivered all three (v0.20); their entries are gone because an
    # entry for a translated name is dead weight that goes stale.
    # Graphics primitives and toys, each self-contained
    "TILE": 2, "Tilemap": 2, "Tilemap(": 2, "Turtle": 2,
    "Frame": 2, "Frame(": 2, "Ray": 2, "Ray(": 2,
    "Mandelbrot": 2,
    "Draw3D": 2, "DRAW3D(": 2,

    # ---- PIO: the steer was given, 2026-08-22 ----------------------
    # The decision: a SEPARATE ASSEMBLER, built from MMBasic's own PIO
    # assembler code, whose output a BASIC program IMPORTS.  So the
    # instruction set and the directives - the language inside the
    # language - never enter the translator at all; they are the input
    # of the assembler, exactly as C is the input of cc and not of
    # mmbc.  Category 4 is therefore their honest home: deliberately
    # out OF THE TRANSLATOR, alive elsewhere.
    "_wrap target": 4, "_wrap": 4, "_line": 4, "_program": 4,
    "_end program": 4, "_side set": 4, "_label": 4,
    "Jmp": 4, "Wait": 4, "In": 4, "Out": 4, "Push": 4, "Pull": 4,
    "Mov": 4, "Nop": 4, "Set": 4,
    "IRQ SET": 4, "IRQ WAIT": 4, "IRQ CLEAR": 4, "IRQ NOWAIT": 4,
    "IRQ PREV": 4, "IRQ NEXT": 4, "IRQ": 4,
    # What mmbc DOES still owe is the runtime surface a program uses
    # around an imported binary: load a program, configure and start a
    # state machine, feed and drain the FIFOs, and the Pio( reads.
    # That is ordinary category-2 work now, much smaller than the
    # block looked when it was all-or-nothing.
    "PIO": 2, "Pio(": 2,

    # ---- 3: possible, wants your steer first -----------------------
    # These want a decision about the machine, not just work.
    "RESOLUTION": 3, "Refresh": 3, "GetScanLine": 3,
    "Memory": 3,                    # POKE's family; no MMU to disagree
    # WS2812 and Bitstream sat here as the interrupts-off pair until
    # they SHIPPED on 2026-08-22 through PLAN-pioout's fixed PIO1
    # programs (NOTES[3] tells the story).  Their entries are gone
    # because an entry for a translated name is dead weight that goes
    # stale - the rule this file states for Json$/WatchDog/CPU above.

    # ---- 4: deliberately out ---------------------------------------
    # Immediate-mode and interpreter-only: a translated program is
    # compiled and run, not typed at a prompt.
    "List": 4, "Run": 4, "Trace": 4, "Execute": 4, "New": 4,
    "Edit": 4, "Edit File": 4, "VAR": 4, "Library": 4, "Autosave": 4,
    # Calc and FM were at 2 until the 2026-08-23 audit read them: Calc
    # is gated on `console` in MMBasic's own tokeniser (MMBasic.c:1349)
    # and FM is Editor.c's file manager, sitting beside Edit above.
    # Both are this rule, not exceptions to it.
    "Calc": 4, "FM": 4,
    "Chain": 4, "Configure": 4, "Help": 4, "Ram": 4,
    "XModem": 4, "YModem": 4,
    "CMM2 Load": 4, "CMM2 Run": 4,
    "Update Firmware": 4,
    "Eval(": 4,                     # wants an interpreter at run time
    "CSub": 4, "End CSub": 4,       # a compiler links objects
    # Not "reads of state the kernel keeps", as this table used to say.
    # AllCommands.h points Interrupt at cmd_csubinterrupt: it arms a
    # CSUB as a handler, so it goes wherever CSub goes.  IReturn returns
    # from a handler written as a LABEL or a LINE NUMBER, and
    # int_handler() refuses both by design - compiled code cannot be
    # jumped into from a poll site - so a translated handler is a SUB
    # and END SUB is its return.  SETTICK, ON KEY and SETPIN INTx are
    # all in already (PLAN-interrupts phase 1).
    "Interrupt": 4, "IReturn": 4,
    "Drive": 4,                     # one filesystem, mounted
    #
    # INTERPRETER-INTERNAL SPELLINGS.  MMBasic ran out of slots in its
    # function table, so it COMMENTED SOME OUT and made the tokeniser
    # rewrite them into one shared entry that takes a selector letter.
    # All four sit inside Functions.c's "@cond ... excluded from the
    # documentation" block; a BASIC program never writes them.
    #
    #   base$(b, n [, w])  <- Hex$( Oct$( Bin$(     "utility function
    #                                               used by HEX$(),
    #                                               OCT$() and BIN$()"
    #   SChange$(sel, ...) <- Left$( Right$( UCase$( LCase$(
    #   TopBottom(sel,...) <- Max( Min(
    #   ~(sel)             <- the flat MM.* reads (MM.HRES, MM.VRES,
    #                         MM.VER, MM.I2C, MM.FONTHEIGHT/WIDTH,
    #                         MM.HPOS/VPOS, MM.ONEWIRE, MM.ERRNO,
    #                         MM.ERRMSG$, MM.DEVICE$, MM.CMDLINE$, ...)
    #
    # We translate SOURCE TEXT, not MMBasic's tokenised form, so the
    # wrapper spelling never reaches us.  What matters is the real
    # functions, and all of them are in: Hex$ Oct$ Bin$ Left$ Right$
    # UCase$ LCase$ Max Min, and 14 of the flat MM.* reads.
    "base$(": 4, "SChange$(": 4, "TopBottom(": 4, "~(": 4,

    # ---- 5: not applicable to this machine -------------------------
    # Hardware the PC3 does not have, or a device the kernel owns.
    "LCD": 5, "I2CLCD": 5,
    "Wii": 5, "Wii Classic": 5, "Wii Nunchuck": 5,
    "Camera": 5, "Touch(": 5,
    # The whole GPS and astronomy family, all four in io/GPS.c and
    # sharing its state: GPS( reads the receiver, cmd_star serves both
    # STAR (which errors unless GPSvalid) and ASTRO, and LOCATION sets
    # the observer's latitude, longitude and date.  This is arithmetic
    # over an NMEA stream with no reason to live in the translator - it
    # is a standalone program, the shape loadjpg and loadpng already
    # take.  Not a gap in the language.
    "GPS(": 5, "Star": 5, "Astro": 5, "Location": 5,
    "MsgBox(": 5, "CtrlVal(": 5, "Click(": 5, "Backlight": 5,
    # WEB sat here as "not applicable" until v0.20 shipped the whole
    # family through the kernel's own sockets - the judgement was about
    # the Pico W's radio API, and the machine grew a better answer.
    "Gamepad": 5, "TMC22xx": 5,
    "Device": 5, "DEVICE(": 5,
    "Keyboard": 5, "Mouse": 5,      # the kernel owns the USB HID devices
    # MMBasic's second SPI bus lands on the pins the SD card uses, and
    # the PC3 does not break those out to any header, so there is
    # nothing a program could talk to.  SPI0 is the one that is wired
    # (PC3-SPI0 notes) and it is in.
    "SPI2": 5, "SPI2(": 5,
    # Stepper was category 2 until the 2026-08-23 audit read it: 6,950
    # lines behind a 100kHz timer interrupt (io/stepper.h:336) - and
    # MMBasic itself REFUSES the whole command whenever I2S audio is
    # configured (io/stepper.c:3752), which on the PC3 is permanent
    # (sound.c:1157).  Replicating the reference exactly therefore
    # means the command errors on every PC3 there is, so "not
    # applicable to this machine" is the reference's own verdict rather
    # than ours.
    "Stepper": 5,
}

NAMES = {
    1: "Finish what is already there",
    2: "Real value, moderate work",
    3: "Possible, wants your steer first",
    4: "Deliberately out",
    5: "Not applicable to this machine",
    "FN": "FALSE NEGATIVE - implemented, invisible to the scan",
}

# Prose that belongs IN the generated document, per category - decided
# approaches, not just name lists.  mkstatus.py prints an entry after
# its category's lists.  Judgement text lives here with the judgements,
# so a regeneration can never lose a decision.
NOTES = {
    2: """`PIO` and `Pio(` are here as the RUNTIME surface only - see the
category 4 note for the decided split: the PIO assembly language gets
its own assembler and never enters the translator.  What mmbc owes is
what a program does around an imported binary: load it, configure and
start a state machine, feed and drain the FIFOs, read `Pio(`.

None of the peripherals here needs interrupts disabled - checked
against the reference's actual loops, 2026-08-22.  `Humid` is an
unmasked microsecond poll whose checksum catches a corrupted read, IR
send is an unmasked toggle loop, `Pulsin(` and `Distance(` are plain
busy-waits.

**But "does MMBasic mask?" was the wrong question for this machine**,
and the 2026-08-23 audit says so: our 5ms system tick holds `di()`
across its entire body whatever else is running, and any userland spin
longer than 5ms gets the USB host pump injected into it.  A measuring
loop is therefore interrupted here even when the reference's is not.
`Humid` survives that on its checksum and `IR SEND` can go through the
shipped BITSTREAM PIO path instead of a loop, but `Pulsin(` and
`Distance(` want the worst-case interruption measured on the board
before either is promised.  `OneShot` and IR RECEIVE were never part of
that review: both are GPIO edge interrupts with their own timers in the
reference, not userland register work.""",
    3: """(`WS2812` and `Bitstream` used to sit here as MMBasic's two
genuinely interrupts-off commands, rejected for bit-banging on a
multi-process machine.  They SHIPPED 2026-08-22 through PLAN-pioout's
fixed PIO1 programs - hardware drives the wire, the machine keeps
running, and the counting inputs proved the streams exact on the
board.  `DEVICE SERIALRX/TX`, the other maskers, stay in category 5
with the rest of `Device`.)""",
    4: """The 22 PIO assembly names (`Jmp` ... `Set`, the `IRQ` rows and the
`_label`/`_wrap`/`_program` directives) are out **of the translator by
design, not out of the machine** - decided 2026-08-22.  The plan: a
SEPARATE ASSEMBLER, built from MMBasic's own PIO assembler code, whose
output a BASIC program imports; the assembly language is that tool's
input exactly as C is `cc`'s input and not `mmbc`'s.  This keeps the
translator's PIO obligation to the small runtime surface noted under
category 2, instead of a language inside the language.""",
}

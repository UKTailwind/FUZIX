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
    "ADC": 2,

    # ---- 2: real value, moderate work ------------------------------
    # Peripherals: a pin here is a register access, not a syscall, so
    # each is a small userland driver (PC3-IO-PLAN.md).
    "WS2812": 2, "IR": 2, "Humid": 2, "Distance(": 2, "Pulsin(": 2,
    "OneShot": 2, "Bitstream": 2, "SYNC": 2, "Slew": 2, "Keypad": 2,
    "Stepper": 2, "FM": 2, "Calc": 2,
    # Json$(, WatchDog and CPU were category 2 until the WEB campaign
    # delivered all three (v0.20); their entries are gone because an
    # entry for a translated name is dead weight that goes stale.
    # Graphics primitives and toys, each self-contained
    "TILE": 2, "Tilemap": 2, "Tilemap(": 2, "Turtle": 2,
    "Frame": 2, "Frame(": 2, "Ray": 2, "Ray(": 2,
    "Mandelbrot": 2,
    "Draw3D": 2, "DRAW3D(": 2,

    # ---- 3: possible, wants your steer first -----------------------
    # The PIO block is all or nothing and is the biggest single win
    # left; it is also a language inside the language.
    "PIO": 3, "Pio(": 3,
    "_wrap target": 3, "_wrap": 3, "_line": 3, "_program": 3,
    "_end program": 3, "_side set": 3, "_label": 3,
    "Jmp": 3, "Wait": 3, "In": 3, "Out": 3, "Push": 3, "Pull": 3,
    "Mov": 3, "Nop": 3, "Set": 3,
    "IRQ SET": 3, "IRQ WAIT": 3, "IRQ CLEAR": 3, "IRQ NOWAIT": 3,
    "IRQ PREV": 3, "IRQ NEXT": 3, "IRQ": 3,
    # These want a decision about the machine, not just work.
    "RESOLUTION": 3, "Refresh": 3, "GetScanLine": 3,
    "Memory": 3,                    # POKE's family; no MMU to disagree

    # ---- 4: deliberately out ---------------------------------------
    # Immediate-mode and interpreter-only: a translated program is
    # compiled and run, not typed at a prompt.
    "List": 4, "Run": 4, "Trace": 4, "Execute": 4, "New": 4,
    "Edit": 4, "Edit File": 4, "VAR": 4, "Library": 4, "Autosave": 4,
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
}

NAMES = {
    1: "Finish what is already there",
    2: "Real value, moderate work",
    3: "Possible, wants your steer first",
    4: "Deliberately out",
    5: "Not applicable to this machine",
    "FN": "FALSE NEGATIVE - implemented, invisible to the scan",
}

' What PicoMite programs ask before they start.  MM.VER is the firmware
' version the translator follows (7.000055 for 7.00.00RC5), so a check
' against 6.03.02b11 passes; MM.INFO$(LCDPANEL) names the mode, "MODE1"
' and on, or is "" with no display; PSRAM SIZE is 8 MB on a PC3 and 0
' with none.
' The answers differ between a board, pc3host and the gates, so the
' shapes are what is printed.
Print "ver"; MM.VER > 6.030211
d$ = MM.Info$(LCDPANEL)
Print "lcdpanel"; Instr(d$, "MODE") = 1 Or d$ = ""
p = MM.Info(PSRAM SIZE)
Print "psram"; p = 0 Or p = 8388608
' PEEK(VAR v, n): the byte n into a variable's storage
Dim t%(3) = (1, 2, 3, 4)
Print "peekvar"; Peek(VAR t%(), 8)
' the BBC Micro's ENVELOPE and SOUND, silent where there is no sound
Play BBC Envelope 1, 1, 0, 0, 0, 0, 0, 0, 126, -50, 0, 0, 126, 0
Play BBC Sound 1, 1, 100, 2
Play Stop
Print "bbc ok"
' MM.INFO(HEAP): what arrays and strings have left - bcrun's heap,
' or 0 in a build whose heap is the C library's
Print "heap"; MM.Info(HEAP) >= 0
' POKE DISPLAY VRES to the height the mode already has: a no-op
Poke Display VRes MM.VRes
Print "vres ok"

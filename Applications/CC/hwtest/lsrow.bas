' A whole display row in one SPI call - what the LONGSTRING data form
' is for.
'
' The ILI9341 is on SPI0 exactly as qnh.bas wires it.  A 240-pixel row
' of RGB565 is 480 bytes and a BASIC string holds 255, so qnh's own
' fillrect builds a 100-pixel run and repeats it: 240x160 pixels is 384
' separate SPI calls.  The kernel never had that limit - struct
' spi_xfer takes any length - so this sends the same pixels as ONE call
' per row, or 160 calls, and times both.
'
' Same picture on the screen either way.  That is the check: two green
' bands that look identical, drawn by two different routes.
Const DCPIN = 5, RSTPIN = 6, CSPIN = 7
Const SW = 240, SH = 320
Const GREEN = &H07E0, BLUE = &H001F, BLACK = 0
Dim integer row(70)                     ' 560 bytes: one 480-byte row
Dim integer i, t1, t2
Dim run$, px$

SetPin CSPIN, DOut : SetPin DCPIN, DOut : SetPin RSTPIN, DOut
Pin(CSPIN) = 1 : Pin(DCPIN) = 1
SetPin 2, 3, 4, SPI
SPI Open 62500000, 0

' reset and initialise, as qnh does
Pin(RSTPIN) = 0 : Pause 20 : Pin(RSTPIN) = 1 : Pause 150
cmd(&H01) : Pause 150
cmd(&H11) : Pause 150
cmd1(&H3A, &H55)
cmd(&H29) : Pause 100
cmd1(&H36, &H48)
Print "display initialised"

' --- the old way: a 100-pixel run, repeated ---
px$ = Chr$(GREEN >> 8) + Chr$(GREEN And 255)
run$ = ""
For i = 1 To 100
  run$ = run$ + px$
Next i
t1 = Timer
fill_chunked(0, 0, SW, 150, run$)
t1 = Timer - t1
Print "chunked (100 px a call): "; Str$(t1, 0, 1); " ms"

' --- the new way: one long string, one call a row ---
LongString Clear row()
For i = 1 To SW
  LongString Append row(), Chr$(BLUE >> 8)
  LongString Append row(), Chr$(BLUE And 255)
Next i
Print "row long string holds "; LLen(row()); " bytes"
t2 = Timer
fill_rows(0, 160, SW, 150)
t2 = Timer - t2
Print "one call a row:          "; Str$(t2, 0, 1); " ms"

' WHAT THIS IS AND IS NOT FOR.  Measured on the board: 32 ms against
' 30 at 24 MHz, 13 against 12 at 62.5 - about a millisecond, which is
' 210 saved system calls at roughly 5 us each.  The SPI bus dominates
' (72,000 bytes is 24 ms of clock at 24 MHz), so this is NOT a speed
' feature and should not be sold as one.
'
' What it is for is that the row can be HELD AT ALL.  A BASIC string
' stops at 255 bytes; this row is 480, and a whole 240x320 frame is
' 153,600.  Before it, a program could not assemble either, and the
' work-around above - build a 100-pixel run, loop until the count runs
' out - had to appear in every program that drew anything.
Print "saved "; Str$(t1 - t2, 0, 0); " ms of ";
Print Str$(t1, 0, 0); " - the bus dominates, so the point is not speed"
SPI Close
Print "done - the two bands should look the same"
End

Sub fill_chunked(x As Integer, y As Integer, w As Integer, h As Integer, r$)
  Local integer total, chunk
  setwin(x, y, x + w - 1, y + h - 1)
  total = w * h
  Do While total > 0
    chunk = 100
    If chunk > total Then chunk = total
    SPI Write chunk * 2, Left$(r$, chunk * 2)
    total = total - chunk
  Loop
  Pin(CSPIN) = 1
End Sub

' One call per row, and the row is 480 bytes.  This is the statement
' that could not be written before.
Sub fill_rows(x As Integer, y As Integer, w As Integer, h As Integer)
  Local integer j
  setwin(x, y, x + w - 1, y + h - 1)
  For j = 1 To h
    SPI Write w * 2, LongString row()
  Next j
  Pin(CSPIN) = 1
End Sub

Sub cmd(c As Integer)
  Pin(CSPIN) = 0 : Pin(DCPIN) = 0
  SPI Write 1, c
  Pin(CSPIN) = 1
End Sub

Sub cmd1(c As Integer, d1 As Integer)
  Pin(CSPIN) = 0 : Pin(DCPIN) = 0
  SPI Write 1, c
  Pin(DCPIN) = 1
  SPI Write 1, d1
  Pin(CSPIN) = 1
End Sub

Sub setwin(x0 As Integer, y0 As Integer, x1 As Integer, y1 As Integer)
  Pin(CSPIN) = 0 : Pin(DCPIN) = 0
  SPI Write 1, &H2A
  Pin(DCPIN) = 1
  SPI Write 4, x0 >> 8, x0 And 255, x1 >> 8, x1 And 255
  Pin(DCPIN) = 0
  SPI Write 1, &H2B
  Pin(DCPIN) = 1
  SPI Write 4, y0 >> 8, y0 And 255, y1 >> 8, y1 And 255
  Pin(DCPIN) = 0
  SPI Write 1, &H2C
  Pin(DCPIN) = 1
End Sub

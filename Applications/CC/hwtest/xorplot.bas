' The XOR pattern, and a readback that says what actually happened.
'
' Same arithmetic as the reported program - already verified to produce
' identical values under gcc and under our own compiler, so the maths is
' not in question.  What this adds is: draw it, then ASK THE SCREEN what
' is there, so "black screen" can be told apart from "drawn but not
' visible".
'
' Run it twice: as it stands (no MODE, so the text console) and with the
' MODE 2 line uncommented.

Option Explicit
Option Default Float
Dim t, cc, white, black, i
Dim Integer x, y

' Mode 2

t = 0
For y = 0 To 239
  For x = 0 To 319
    cc = Abs(t + ((x - t) Xor (x + t)) ^ 3) Mod 997
    If cc < 97 Then
      cc = 0
    Else
      cc = &Hffffff
    EndIf
    Pixel x, y, cc
  Next x
  t = t + 1
Next y

' Now read it back.  A sample down the middle of the picture, well away
' from anything the shell might have redrawn.
white = 0
black = 0
For y = 100 To 200
  For x = 0 To 319
    If Pixel(x, y) <> 0 Then
      white = white + 1
    Else
      black = black + 1
    EndIf
  Next x
Next y

Print "rows 100-200 of the picture:"
Print "  non-black pixels: "; white
Print "  black pixels:     "; black
Print "(a white background means non-black should be much the larger)"

' And what colour a lit pixel actually reads back as
Pixel 5, 5, &Hffffff
Print "a pixel set to &Hffffff reads back as "; Hex$(Pixel(5, 5), 6)

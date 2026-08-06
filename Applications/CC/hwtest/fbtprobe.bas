' Is graphics-mode text actually being drawn, and does it scroll?
'
' Not something to catch by eye: the program reads the pixels back and
' reports, then returns to MODE 1 so the numbers survive on the console.
'
' PIXEL() reads through the same target PRINT drew into, so this asks
' the screen what really happened.

Option Explicit
Option Default Float
Dim Integer x, y, ink, i, top, bot

Mode 2
Colour Rgb(WHITE), Rgb(BLACK)
Cls

' --- 1. is anything drawn at all? ---
' A capital H at the origin: an 8x12 cell that must contain ink.
Print @(0, 0) "H"
ink = 0
For y = 0 To 11
  For x = 0 To 7
    If Pixel(x, y) <> 0 Then ink = ink + 1
  Next x
Next y

' --- 2. does it land where it is told? ---
Dim Integer ink2
Print @(80, 60) "H"
ink2 = 0
For y = 60 To 71
  For x = 80 To 87
    If Pixel(x, y) <> 0 Then ink2 = ink2 + 1
  Next x
Next y

' --- 3. does a PRINT past the bottom scroll? ---
' Fill the screen and then some.  If it scrolls, the top line is no
' longer blank-then-H: it holds whatever line ended up there.  Count
' ink across the top row of cells before and after the overflow.
Cls
For i = 1 To 19
  Print "line"; i
Next i
top = 0
For y = 0 To 11
  For x = 0 To 159
    If Pixel(x, y) <> 0 Then top = top + 1
  Next x
Next y
' now push past the bottom
For i = 20 To 24
  Print "line"; i
Next i
bot = 0
For y = 0 To 11
  For x = 0 To 159
    If Pixel(x, y) <> 0 Then bot = bot + 1
  Next x
Next y

Mode 1
Print "ink in the cell at (0,0):    "; ink;   "  (0 means nothing drawn)"
Print "ink in the cell at (80,60):  "; ink2;  "  (0 means @ ignored)"
Print "top row ink before overflow: "; top
Print "top row ink after overflow:  "; bot;  "  (differs => it scrolled)"

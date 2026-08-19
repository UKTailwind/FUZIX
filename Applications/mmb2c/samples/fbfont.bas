' TEXT and the nine built-in fonts - measured, not looked at.
'
' Every case draws and then reads the pixels back, so the numbers say
' what actually reached the framebuffer.  The interesting one is the
' bounding box of a single glyph: it must GROW with the font, because
' each font has its own cell and the layout arithmetic is done against
' the size the kernel reports.  If TEXT were assuming font 1's 8x12 the
' boxes would all come out the same.

Option Explicit
Option Default Float
Dim Integer x, y, f, ink, x1, y1, i
' Collected, not printed: in a graphics mode PRINT draws on the HDMI
' screen, so anything reported before returning to the console is
' invisible down the serial line.
Dim Integer fink(9), fw(9), fh(9)

Mode 2
Colour Rgb(WHITE), Rgb(BLACK)

' --- 1. one glyph per font: ink, and how far it reaches ---
For f = 1 To 9
  Cls
  ' Font 6 has only the digits, so give it one it owns.
  If f = 6 Then
    Text 0, 0, "8", "LT", f
  Else
    Text 0, 0, "H", "LT", f
  EndIf
  ink = 0 : x1 = -1 : y1 = -1
  For y = 0 To 55
    For x = 0 To 39
      If Pixel(x, y) <> 0 Then
        ink = ink + 1
        If x > x1 Then x1 = x
        If y > y1 Then y1 = y
      EndIf
    Next x
  Next y
  fink(f) = ink : fw(f) = x1 + 1 : fh(f) = y1 + 1
Next f

' --- 2. scale multiplies the cell ---
Cls
Text 0, 0, "H", "LT", 1, 1
ink = 0
For y = 0 To 11
  For x = 0 To 7
    If Pixel(x, y) <> 0 Then ink = ink + 1
  Next x
Next y
Dim Integer ink4
Cls
Text 0, 0, "H", "LT", 1, 2
ink4 = 0
For y = 0 To 23
  For x = 0 To 15
    If Pixel(x, y) <> 0 Then ink4 = ink4 + 1
  Next x
Next y

' --- 3. justification.  Centred on a point, the text must straddle it;
'        right-justified it must END there and leave the far side clear.
Cls
Text 160, 100, "MIDDLE", "CM", 1
Dim Integer lft, rgt
lft = 0 : rgt = 0
For y = 94 To 105
  For x = 136 To 159
    If Pixel(x, y) <> 0 Then lft = lft + 1
  Next x
  For x = 160 To 183
    If Pixel(x, y) <> 0 Then rgt = rgt + 1
  Next x
Next y

Cls
Text 160, 100, "RIGHT", "RT", 1
Dim Integer before, after
before = 0 : after = 0
For y = 100 To 111
  For x = 120 To 159
    If Pixel(x, y) <> 0 Then before = before + 1
  Next x
  For x = 160 To 199
    If Pixel(x, y) <> 0 Then after = after + 1
  Next x
Next y

' --- 4. transparent paper leaves what is underneath, opaque paper does
'        not.  Both counted UNDER the text, which is where it shows.
Dim Integer clear1, opaque
Cls
Line 0, 200, 319, 200, , Rgb(RED)
Text 0, 195, "OVER", "LT", 1, 1, Rgb(WHITE), -1
clear1 = 0
For x = 0 To 31
  If Pixel(x, 200) = Rgb(RED) Then clear1 = clear1 + 1
Next x

Cls
Line 0, 200, 319, 200, , Rgb(RED)
Text 0, 195, "OVER", "LT", 1, 1, Rgb(WHITE), Rgb(BLUE)
opaque = 0
For x = 0 To 31
  If Pixel(x, 200) = Rgb(RED) Then opaque = opaque + 1
Next x

' --- 5. vertical orientation steps DOWN by the cell height ---
Cls
Text 0, 0, "AB", "LTV", 1
Dim Integer top, second
top = 0 : second = 0
For y = 0 To 11
  For x = 0 To 7
    If Pixel(x, y) <> 0 Then top = top + 1
  Next x
Next y
For y = 12 To 23
  For x = 0 To 7
    If Pixel(x, y) <> 0 Then second = second + 1
  Next x
Next y

' --- 6. a PRINT after a TEXT carries on where the text ended, because
'        both use the cursor MMBasic calls CurrentX/CurrentY.
Cls
Text 0, 50, "AB", "LT", 1
Print "C";
Dim Integer third
third = 0
For y = 50 To 61
  For x = 16 To 23
    If Pixel(x, y) <> 0 Then third = third + 1
  Next x
Next y

' --- 7. CLS with a colour floods the write target; a bare CLS uses the
'        background COLOUR, which is MMBasic's gui_bcolour default.
Dim Integer blue, green
Cls Rgb(BLUE)
blue = 0
For x = 0 To 304 Step 16
  If Pixel(x, 120) = Rgb(BLUE) Then blue = blue + 1
Next x

Colour Rgb(WHITE), Rgb(GREEN)
Cls
green = 0
For x = 0 To 304 Step 16
  If Pixel(x, 120) = Rgb(GREEN) Then green = green + 1
Next x
Colour Rgb(WHITE), Rgb(BLACK)

Mode 1
Print
Print "font   ink   width  height   (cell must grow with the font)"
For f = 1 To 9
  Print f, fink(f), fw(f), fh(f)
Next f
Print
Print "scale 1 ink "; ink; ", scale 2 ink "; ink4; "  (x2 cell => x4 ink)"
Print "centred:  left "; lft; "  right "; rgt; "   (both > 0 = straddles)"
Print "right-justified: before "; before; " after "; after; " (after = 0)"
Print "red under transparent text: "; clear1; " of 32  (> 0 = see-through)"
Print "red under opaque text:      "; opaque; " of 32  (0 = covered)"
Print "vertical: top cell "; top; "  cell below "; second; " (both > 0)"
Print "PRINT after TEXT lands in the 3rd cell: "; third; "  (> 0)"
Print "CLS Rgb(BLUE) samples blue:  "; blue; " of 20"
Print "bare CLS uses background:    "; green; " of 20"

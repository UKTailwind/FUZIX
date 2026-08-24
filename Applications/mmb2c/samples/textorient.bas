' TEXT's five orientations - measured, not looked at.
'
' The third letter of TEXT's justify$ turns the glyphs: N upright, V a
' column, I upside down, U a quarter turn anticlockwise, D clockwise.
' Turning them moves the CELL as well, and that is the part a program
' can get wrong without anyone noticing: the character comes out the
' right way up and a whole cell away from where it belongs.
'
' So every number here is anchor-RELATIVE.  The anchor is the pixel the
' character would have rotated about, so:
'
'   N   the cell's top-left     0..7, 0..11    (font 1, scale 1)
'   V   the same - V turns the PEN, not the glyph
'   I   its bottom-right       -7..0, -11..0
'   U   its left edge, one below the bottom
'   D   its top-right           -11..0, 0..7   (the cell is on its side)
'
' Run it on a PicoMite and on a PC3 and compare the two transcripts.
' They must agree line for line; a one-pixel difference is a difference.

Option Explicit
Option Default Float
Dim Integer k, ax = 150, ay = 150
Dim Integer bx0, bx1, by0, by1
' Collected, not printed: in a graphics mode PRINT draws on the screen,
' so anything reported before returning to the console is invisible
' down the serial line.
Dim Integer c0(4), c1(4), c2(4), c3(4)
Dim Integer n0(4), n1(4), n2(4), n3(4)
Dim Integer r0(4), r1(4), r2(4), r3(4)
Dim Integer m0(4), m1(4), m2(4), m3(4)
Dim String j$

Mode 2
Colour Rgb(WHITE), Rgb(BLACK)

For k = 0 To 4
  j$ = Mid$("NVIUD", k + 1, 1)

  ' 1. one glyph, top-left justified: where does its CELL sit?  Paper
  '    blue, ink white, so the cell is everything that is not black.
  Cls
  Text ax, ay, "Y", "LT" + j$, 1, 1, Rgb(WHITE), Rgb(BLUE)
  bbox(110, 110, 80, 80, -1)
  c0(k) = bx0 - ax : c1(k) = bx1 - ax : c2(k) = by0 - ay : c3(k) = by1 - ay

  ' 2. the INK inside it.  A turned glyph is the upright one reflected
  '    or rotated within the same cell, so this is what proves the
  '    character itself turned and not just the cell.
  bbox(110, 110, 80, 80, Rgb(WHITE))
  n0(k) = bx0 - ax : n1(k) = bx1 - ax : n2(k) = by0 - ay : n3(k) = by1 - ay

  ' 3. two glyphs: which way does the pen walk?
  Cls
  Text ax, ay, "YY", "LT" + j$, 1, 1, Rgb(WHITE), Rgb(BLUE)
  bbox(110, 110, 80, 80, -1)
  r0(k) = bx0 - ax : r1(k) = bx1 - ax : r2(k) = by0 - ay : r3(k) = by1 - ay

  ' 4. centred and middled, which must straddle the anchor whichever
  '    way the text runs - the justification arithmetic changes sign
  '    with the orientation and this is what catches a wrong one.
  Cls
  Text ax, ay, "YY", "CM" + j$, 1, 1, Rgb(WHITE), Rgb(BLUE)
  bbox(110, 110, 80, 80, -1)
  m0(k) = bx0 - ax : m1(k) = bx1 - ax : m2(k) = by0 - ay : m3(k) = by1 - ay
Next k

Mode 1
Print
Print "TEXT orientations, font 1 scale 1, relative to the anchor"
Print
Print "     one glyph LT       its ink           two glyphs LT"
For k = 0 To 4
  Print Mid$("NVIUD", k + 1, 1); "  ";
  Print Str$(c0(k), 4); Str$(c1(k), 4); Str$(c2(k), 4); Str$(c3(k), 4); "   ";
  Print Str$(n0(k), 4); Str$(n1(k), 4); Str$(n2(k), 4); Str$(n3(k), 4); "   ";
  Print Str$(r0(k), 4); Str$(r1(k), 4); Str$(r2(k), 4); Str$(r3(k), 4)
Next k
Print
Print "centred and middled - must straddle the anchor in both axes"
For k = 0 To 4
  Print Mid$("NVIUD", k + 1, 1); "  ";
  Print Str$(m0(k), 4); Str$(m1(k), 4); Str$(m2(k), 4); Str$(m3(k), 4)
Next k

' The bounding box of everything in a rectangle that matches `want`,
' or of everything not black when want is -1.  Answers through the four
' globals rather than an array parameter, so the reading is the same on
' both machines.
Sub bbox(px As Integer, py As Integer, pw As Integer, ph As Integer, want As Integer)
  Local Integer x, y, p, hit
  bx0 = 9999 : bx1 = -9999 : by0 = 9999 : by1 = -9999
  For y = py To py + ph - 1
    For x = px To px + pw - 1
      p = Pixel(x, y)
      If want = -1 Then
        hit = (p <> 0)
      Else
        hit = (p = want)
      EndIf
      If hit Then
        If x < bx0 Then bx0 = x
        If x > bx1 Then bx1 = x
        If y < by0 Then by0 = y
        If y > by1 Then by1 = y
      EndIf
    Next x
  Next y
End Sub

' --- the reference transcript --------------------------------------
'
' Taken from a real PicoMite running MMBasic 6.03.02, MODE 2, on
' 2026-08-24, and reproduced LINE FOR LINE by a PC3 the same day -
' translated and compiled by the card's own mmbc and cc.  A PC3 must go
' on printing these same numbers.
'
'  TEXT orientations, font 1 scale 1, relative to the anchor
'
'       one glyph LT       its ink           two glyphs LT
'  N     0   7   0  11      1   5   1   9      0  15   0  11
'  V     0   7   0  11      1   5   1   9      0   7   0  23
'  I    -7   0 -11   0     -5  -1  -9  -1    -15   0 -11   0
'  U     0  11  -8  -1      1   9  -6  -2      0  11 -16  -1
'  D   -11   0   0   7     -9  -1   1   5    -11   0   0  15
'  centred and middled - must straddle the anchor in both axes
'  N    -8   7  -6   5
'  V    -4   3 -12  11
'  I    -7   8  -5   6
'  U    -6   5  -8   7
'  D    -5   6  -8   7
'
' READ THE U ROW.  Its cell ends one pixel ABOVE the anchor where N, I
' and D all have the anchor inside the cell.  That is the reference's
' own asymmetry - GUIPrintChar subtracts width*scale for a quarter turn
' anticlockwise where every other case subtracts one less - and it is
' copied rather than corrected, because two machines side by side must
' put the same pixel in the same place.  See mmb_gfx_text.h.

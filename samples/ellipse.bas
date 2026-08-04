' Outline ellipse with a stretched aspect - the bug MMBasic just fixed.
'
' With aspect > 1 the x coordinates are scaled, so consecutive points of
' the circle algorithm are more than a pixel apart and plotting them
' singly leaves holes.  Worst along the flat top and bottom, where x
' moves fastest per step, and at the 45 degree point where the two
' octants stop short of each other.
'
' Counted rather than eyeballed: a run of lit pixels broken by gaps
' shows up as many separate runs along one scan line.  A sound outline
' gives one run per side.

Option Explicit
Option Default Float
Dim Integer x, y, runs, lit, was, cx, cy, r, i
Dim Integer rlit(3), rruns(3)

Mode 2
Colour Rgb(WHITE), Rgb(BLACK)
Cls

cx = 160 : cy = 120 : r = 30

' aspect 3: 30 tall, 90 wide
Circle cx, cy, r, 1, 3, Rgb(WHITE)

' Count runs along the row just below the top of the ellipse, where the
' curve is flattest and the stepping is coarsest.
For y = cy - r To cy - r + 3
  runs = 0 : was = 0 : lit = 0
  For x = 0 To 319
    If Pixel(x, y) <> 0 Then
      lit = lit + 1
      If was = 0 Then runs = runs + 1
      was = 1
    Else
      was = 0
    EndIf
  Next x
  rlit(y - (cy - r)) = lit
  rruns(y - (cy - r)) = runs
Next y

' and across the middle, which should be two runs - left edge, right edge
runs = 0 : was = 0 : lit = 0
For x = 0 To 319
  If Pixel(x, cy) <> 0 Then
    lit = lit + 1
    If was = 0 Then runs = runs + 1
    was = 1
  Else
    was = 0
  EndIf
Next x

Mode 1
For i = 0 To 3
  Print "row"; cy - r + i; ": lit="; rlit(i); " runs="; rruns(i); "  (1 run = no holes)"
Next i
Print "middle row: lit="; lit; " runs="; runs; " (want 2)"

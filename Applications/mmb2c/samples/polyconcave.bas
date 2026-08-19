' POLYGON concave fill, with sample points worked out from the geometry
' rather than guessed at.
'
' The shape is a rectangle with a V cut into its bottom edge:
'   (20,20) (100,20) (100,100) (60,50) (20,100), closed.
' The V apex points UP to (60,50).
'
' At y=90 the edge crossings are 20, 28, 92, 100 - so the two legs span
' x=20..28 and x=92..100, and everything between is the notch.  A point
' at x=30 is OUTSIDE, which is what the first version of this test got
' wrong: the old min/max fill reported it filled because it bridged
' 20..100, and the test agreed with the bug.
'
' Nothing prints before Mode 1: in a graphics mode PRINT draws on the
' screen and scrolls it, which would move the pixels out from under the
' read-back.
Option Explicit
Option Default Integer
Dim px(4), py(4)
Dim body, leftleg, rightleg, notch, between, outside

Mode 2
Colour Rgb(WHITE), Rgb(BLACK)
Cls

px(0)=20 : py(0)=20
px(1)=100: py(1)=20
px(2)=100: py(2)=100
px(3)=60 : py(3)=50
px(4)=20 : py(4)=100
Polygon 5, px(), py(), Rgb(WHITE), Rgb(YELLOW)

body     = Pixel(60, 30)      ' well inside the solid top
leftleg  = Pixel(24, 90)      ' inside the left leg (20..28)
rightleg = Pixel(96, 90)      ' inside the right leg (92..100)
notch    = Pixel(60, 95)      ' deep in the V
between  = Pixel(50, 90)      ' also in the V, left of centre
outside  = Pixel(10, 60)      ' outside the shape entirely

Mode 1
Print "concave arrowhead, V cut into the bottom"
Print "  body      "; Hex$(body,6);     "  want FFFF00"
Print "  left leg  "; Hex$(leftleg,6);  "  want FFFF00"
Print "  right leg "; Hex$(rightleg,6); "  want FFFF00"
Print "  notch     "; Hex$(notch,6);    "  want 000000"
Print "  in the V  "; Hex$(between,6);  "  want 000000"
Print "  outside   "; Hex$(outside,6);  "  want 000000"

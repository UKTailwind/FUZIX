' Write half only: draw, then one SAVE IMAGE.  No load, no SYSTEM, no
' printing.  Check df after this before running rtest.bas.
MODE 1
For y = 0 To 479
  Line 0, y, 639, y, , 0
Next y
Circle 320, 240, 150, , , RGB(WHITE)
Circle 320, 240, 80, , , RGB(WHITE), RGB(WHITE)
SAVE IMAGE "w1.bmp", 160, 120, 320, 240

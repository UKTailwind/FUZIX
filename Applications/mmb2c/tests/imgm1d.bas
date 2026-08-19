' Is the MODE 1 round trip WRONG, or lossy but stable?
'
' In console mode the framebuffer is one bit per pixel but the colour
' comes from per-cell tile attributes, so a save records colours a load
' can only restore as ink or paper.  If that is all that is happening,
' the first round trip loses something and every one after it is
' identical - r2 = r3.  If loadimage were actually wrong, they would
' keep drifting.
MODE 1
For y = 0 To 479
  Line 0, y, 639, y, , 0
Next y
Circle 320, 240, 150, , , RGB(WHITE)
Circle 320, 240, 80, , , RGB(WHITE), RGB(WHITE)
SAVE IMAGE "r1.bmp", 160, 120, 320, 240
For y = 0 To 479
  Line 0, y, 639, y, , 0
Next y
LOAD IMAGE "r1.bmp", 160, 120
SAVE IMAGE "r2.bmp", 160, 120, 320, 240
For y = 0 To 479
  Line 0, y, 639, y, , 0
Next y
LOAD IMAGE "r2.bmp", 160, 120
SAVE IMAGE "r3.bmp", 160, 120, 320, 240
SYSTEM "sum", "r1.bmp", "r2.bmp", "r3.bmp"
Print "convergence test done"

' MODE 1 round trip with the console kept out of it.
'
' The screen is wiped to black by drawing over it rather than by
' re-entering the mode, which would repaint the console's text; and the
' picture is pure white on pure black, so the one-bit dither has
' nothing to threshold.  If these two files differ, loadimage is wrong
' in 1bpp.  If they match, the earlier mismatch was the console's
' per-cell colours, which a one-bit framebuffer cannot carry.
MODE 1
For y = 0 To 479
  Line 0, y, 639, y, , 0
Next y
Circle 320, 240, 150, , , RGB(WHITE)
Circle 320, 240, 80, , , RGB(WHITE), RGB(WHITE)
Circle 150, 120, 60, 5, , RGB(WHITE)
SAVE IMAGE "n1.bmp"
For y = 0 To 479
  Line 0, y, 639, y, , 0
Next y
LOAD IMAGE "n1.bmp"
SAVE IMAGE "n1b.bmp"
SYSTEM "sum", "n1.bmp", "n1b.bmp"
Print "mode 1 clean round trip done"

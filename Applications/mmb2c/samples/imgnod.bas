' imgloop with the drawing hoisted out of the loop.
'
' imgloop redraws the picture every round, so a checksum that changes
' between rounds has two possible causes: the file round trip broke, or
' the SCREEN was not the same the second time (in MODE 1 the console and
' the graphics share one framebuffer, and the sum output scrolls it).
' Here the picture is drawn once and never touched again, so every round
' saves the same pixels.  If the checksums still drift, the fault is in
' the file path and the drawing is innocent.
MODE 1
For y = 0 To 479
  Line 0, y, 639, y, , 0
Next y
Circle 320, 240, 150, , , RGB(WHITE)
Circle 320, 240, 80, , , RGB(WHITE), RGB(WHITE)
Circle 240, 200, 40, 5, , RGB(WHITE)
For n = 1 To 5
  SAVE IMAGE "n1.bmp", 160, 120, 320, 240
  LOAD IMAGE "n1.bmp", 160, 120
  SAVE IMAGE "n2.bmp", 160, 120, 320, 240
  SYSTEM "sum", "n1.bmp", "n2.bmp"
Next n
Print "no-redraw rounds done"

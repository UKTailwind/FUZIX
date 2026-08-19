' Read half only: wipe the screen, then one LOAD IMAGE of what
' wtest.bas wrote.  No save, no SYSTEM, no printing.  Check df after.
MODE 1
For y = 0 To 479
  Line 0, y, 639, y, , 0
Next y
LOAD IMAGE "w1.bmp", 160, 120

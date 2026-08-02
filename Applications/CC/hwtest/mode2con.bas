' PRINT in a graphics mode, as MMBasic does it.
'
' MODE 2 is 320x240, so the console is 40x20 cells of the same 8x12
' font the text console uses. Text and graphics share the screen: the
' circles below are drawn first and the text lands on top of them.
MODE 2
CIRCLE 160, 120, 100, , , RGB(WHITE)
CIRCLE 160, 120, 60, , , RGB(YELLOW), RGB(BLUE)
LINE 0, 0, 319, 239, , RGB(RED)
LINE 0, 239, 319, 0, , RGB(RED)
Print "MODE 2 console: 40 x 20"
Print "text over graphics"
For i = 1 To 6
  Print "line "; i; " of six"
Next i
Print "done - the shell prompt follows"

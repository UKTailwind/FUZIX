' The other half of pixexit.bas - run that first.
'
' What actually reached the screen.  A separate process, so it can only
' see what the one before it really drew.
Option Explicit
Option Default Float
Dim Integer i, n
n = 0
For i = 0 To 9
  If Pixel(100 + i, 150) <> 0 Then n = n + 1
Next i
Print "on screen after the previous program exited: "; n; " of 10"

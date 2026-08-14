' OPTION CONSOLE SERIAL | SCREEN | BOTH | NONE
'
' A bitmask, the reference's own: 1 serial, 2 screen, 3 both, 0 neither,
' and putConsole tests the two bits separately (PicoMite.c:1174).
'
' Under the gates there is no graphics console, so screen and serial are
' the same tty and only NONE is visibly different.  What this pins is
' that the statement is accepted everywhere a statement can appear, that
' it takes effect where it stands rather than being hoisted, and that
' NONE really does silence PRINT and BOTH brings it back.  The routing
' itself is a board test - the whole point is a picture on one device
' and a trace on the other.

Option explicit
Option default none
Dim integer i

Option console both
Print "1 both"

Option console serial
Print "2 serial"

Option console none
Print "3 THIS MUST NOT APPEAR"

Option console both
Print "4 back"

' it is a run-time statement: inside a loop, and inside a SUB
For i = 1 To 3
  If i = 2 Then Option console none
  Print "loop "; Str$(i)
  Option console both
Next i

quiet_bit
Print "6 after the sub"

Print "CONSOLE DONE"
End

Sub quiet_bit
  Option console none
  Print "5 THIS MUST NOT APPEAR EITHER"
  Option console both
End Sub

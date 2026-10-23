' OPTION EXPLICIT: the ELSEIF that was never tested in explicitelseif is
' tested here, so its undeclared name is "Q is not declared" there, after
' the branch before has printed nothing (FUZIX issue #34).
Option Explicit
Dim a = 1
If a = 0 Then
  Print "zero"
ElseIf q = 1 Then
  Print "q"
End If
Print "not reached"

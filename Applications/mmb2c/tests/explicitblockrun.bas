' OPTION EXPLICIT: the FOR that never ran in explicitblock does run here,
' so its undeclared counter is "I is not declared" where it stands
' (FUZIX issue #25).
Option Explicit
Dim a = 1
Print "ran"
If a = 1 Then
  For i = 1 To 3 : Next
End If
Print "not reached"

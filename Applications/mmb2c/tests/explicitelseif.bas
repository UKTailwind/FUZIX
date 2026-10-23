' OPTION EXPLICIT's "not declared" where findvar raises it (FUZIX issue
' #34): an ELSEIF or a CASE is tested only when the branches before it
' were not taken, so the error is not in the branch before.  And two
' undeclared FOR variables each have a counter of their own: nested,
' sharing one, they never ended.
Option Explicit
Dim a = 0
If a = 0 Then
  Print "zero"
ElseIf q = 1 Then
  Print "q"
Else If q = 2 Then
  Print "q2"
End If
Select Case a
  Case 0
    Print "case 0"
  Case r
    Print "r"
End Select
On Error Ignore
For i = 1 To 10 : For j = 1 To 2 : Next : Next
Print "end"

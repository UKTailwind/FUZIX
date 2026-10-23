' OPTION EXPLICIT: a name not declared, in a line that never runs, is no
' error (FUZIX issue #25) - findvar fails only if the line runs - even
' where the line opens a block, a FOR or an IF, that cannot be dropped.
' A firmware function we lack, GetScanLine, is not translated, rather
' than "not declared".
Option Explicit
Dim a = 0
Print "ran"
If a = 1 Then
  For i = 1 To 3 : Next
  If q > 0 Then Print "q"
End If
If a Then
  Select Case GetScanLine
    Case 1 : Print "one"
  End Select
End If
Print "end"

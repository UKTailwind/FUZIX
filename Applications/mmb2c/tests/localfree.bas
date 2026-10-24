' LOCAL arrays with a run-time bound are given back when the routine
' returns, on every way out (the PicoMite session's review): some 180K
' a call and 1000 calls, more than any of the heaps holds at once
n = 10000
For k = 1 To 1000
  Work k
  t = t + F(k)
Next
Print "done"; t
Sub Work(k)
  Local a(n), s$(n / 100)
  a(n) = k : s$(1) = "x"
  If k Mod 2 Then Exit Sub
  a(0) = 1
End Sub
Function F(k)
  Local b%(n)
  b%(n) = k
  If k Mod 3 = 0 Then
    F = 1
    Exit Function
  EndIf
  F = 0
End Function

' MAX and MIN as fun_max_min: each argument evaluated once, in order,
' as a float; the result is a float; a tie keeps the earlier value.
Dim n%, order$
Function F(x)
  n% = n% + 1 : order$ = order$ + Str$(x) : F = x
End Function
Function R(d)
  ' frame-local temporaries: shared ones would come back 2, not 3
  If d = 0 Then R = 0 Else R = Max(d, R(d - 1))
End Function
Sub S
  n% = 0 : order$ = ""
  Print "sub "; Max(F(7), F(6)); n%; " "; order$
End Sub

bad = 0
For i = 1 To 2000
  If Max(Rnd, 0.5) < 0.5 Then bad = bad + 1
  If Min(Rnd, 0.5) > 0.5 Then bad = bad + 1
Next
Print "rnd outside "; bad
m = Max(F(1), F(3), F(2))
Print "once "; m; n%; " "; order$
n% = 0 : order$ = ""
m = Max(F(1), Min(F(5), F(4)), F(2))
Print "nested "; m; n%; " "; order$
S
Print "recursive "; R(3)
Print "ten "; Max(1, 2, 3, 4, 5, 6, 7, 8, 9, 10); Min(5, 4, 3, 2, 1, 0, -1, -2, -3, -4)
a% = 9007199254740993
b% = Max(a%, 1)
Print "float "; b%
Const C = Max(3, 4)
Dim z(C)
Print "const "; Bound(z(), 1)
Print "mixed "; Max(2, 1.5); Min(2, 1.5)

' MAX and MIN as fun_max_min: each argument evaluated once, in order;
' a tie keeps the earlier value; the result is always a float (FUZIX
' issue #1), and two in one expression keep their own temporaries (#12).
Dim n%, order$
Function F(x)
  n% = n% + 1 : order$ = order$ + Str$(x) : F = x
End Function
Function G%(x%)
  n% = n% + 1 : order$ = order$ + Str$(x%) : G% = x%
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
n% = 0 : order$ = ""
k% = Max(G%(4), Min(G%(9), G%(8)), G%(2))
Print "integer "; k%; n%; " "; order$
n% = 0 : order$ = ""
m = Max(F(1), Max(G%(5), G%(6)))
Print "mixed nest "; m; n%; " "; order$
S
Print "recursive "; R(3)
Print "ten "; Max(1, 2, 3, 4, 5, 6, 7, 8, 9, 10); Min(5, 4, 3, 2, 1, 0, -1, -2, -3, -4)
a% = 9007199254740993
b% = Max(a%, 1)
d% = Max(a%, 1.5)
Print "exact "; b%; " float "; d%
Const C = Max(3, 4)
Dim z(C)
Print "const "; Bound(z(), 1)
Print "mixed "; Max(2, 1.5); Min(2, 1.5)
Print "big "; Max(2000000, 5); Min(-2000000, 5)
x% = 2000000
Print "power "; Max(x%, 3) ^ 40
n% = 0 : order$ = ""
m = Max(F(1), F(3), F(2)) + Min(F(5), F(4), F(6))
Print "two "; m; n%; " "; order$
Read d1, d2
Print "data "; d1; d2
Data Max(1.5, 2, 3, 4, 5), Min(2.5, 1E3, -7, 4, 0.25)

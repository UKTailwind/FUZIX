' ON ERROR SKIP's count is used up at the depth that set it: the
' statements of a FUNCTION the protected statement calls do not spend
' it (PicoMite issue 34).  A SUB that FUNCTION calls runs at its depth,
' so its statements do not either.
Function f(a)
  Local t = a
  t = t + 1
  t = t * 2
  f = t
End Function

Sub bump(v)
  v = v + 1
  v = v + 1
End Sub

Function h(a)
  Local t = a
  bump t
  bump t
  h = t
End Function

Function early(a)
  If a > 0 Then Exit Function
  early = -1
End Function

' 1. a literal SKIP 1: f()'s statements leave the count for the /0
On Error Skip 1
Print f(1) / 0
Print "1 "; MM.ErrMsg$

' 2. the same with the count known only when the line runs
n = 1
On Error Skip n
Print f(2) / 0
Print "2 "; MM.ErrMsg$

' 3. two calls in one statement, then the error
On Error Skip 1
x = f(1) + f(2) + 1 / 0
Print "3 "; x; " "; MM.ErrMsg$

' 4. a SUB called from a FUNCTION counts at the FUNCTION's depth
On Error Skip 1
y = h(1) / 0
Print "4 "; y; " "; MM.ErrMsg$

' 5. EXIT FUNCTION's way out
On Error Skip 1
z = early(1) + early(2) + 1 / 0
Print "5 "; z; " "; MM.ErrMsg$

' 6. the values themselves, untrapped
Print "6 "; f(1); h(1); early(0)

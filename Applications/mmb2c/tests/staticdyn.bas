' A STATIC whose bound is worked out when it is made - a CONST from an
' expression, a variable - is a run-time array, made the first time its
' statement runs (FUZIX issue #31): as a C array its size was not a
' constant and the C did not compile.  Its elements last from one call to
' the next.  And an array DIMmed again after an ERASE, with other bounds,
' is made again with them: the second DIM wrote past the first.
Const K = Max(2, 3) * 2
Const L = Max(2, 3) + 1
Const Q = 10 / 4
Const A = Min(4, 9)
Const B = A * 2
n = 5
S
S
Const K2 = 2.0
Dim z(K2)
Erase z
Dim z(10)
z(10) = 3
Print z(10), Bound(z())
End
Sub S
  Static a1(K), a2(L), a3(Q), a4(Sqr(16)), a5(B), v(n)
  a1(K) = a1(K) + 1 : a2(L) = a2(L) + 2 : a3(3) = a3(3) + 3
  a4(4) = a4(4) + 4 : a5(B) = a5(B) + 5 : v(n) = v(n) + 6
  Print a1(K); a2(L); a3(3); a4(4); a5(B); v(n)
  Print Bound(a1()); Bound(a2()); Bound(a3()); Bound(a4()); Bound(a5()); Bound(v())
End Sub

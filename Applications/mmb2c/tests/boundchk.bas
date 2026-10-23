' BOUND() as fun_bound: the dimension is getint(..., 0, 5); then findvar
' says "Array dimensions" for x() when x is no array (FUZIX issue #8);
' then 0 is OPTION BASE, and a dimension the array does not have is
' "Dimensions" - for a DIMmed array, an array parameter, and a dimension
' worked out as the program runs.
Sub P(b())
  Print "param "; Bound(b()); Bound(b(), 2)
  ON ERROR SKIP 1
  n = Bound(b(), 3)
  Print "param 3: " MM.ERRMSG$
  d = 2
  Print "param d: "; Bound(b(), d)
  d = 4
  ON ERROR SKIP 1
  n = Bound(b(), d)
  Print "param d=4: " MM.ERRMSG$
End Sub
Dim a(3, 4)
Print Bound(a()); Bound(a(), 1); Bound(a(), 2); Bound(a(), 0)
ON ERROR SKIP 1
n = Bound(a(), 3)
Print "dim 3: " MM.ERRMSG$
ON ERROR SKIP 1
n = Bound(a(), 6)
Print "dim 6: " MM.ERRMSG$
x = 5
ON ERROR SKIP 1
n = Bound(x())
Print "scalar: " MM.ERRMSG$
ON ERROR SKIP 1
n = Bound(x(), 0)
Print "scalar 0: " MM.ERRMSG$
d = 0
ON ERROR SKIP 1
n = Bound(x(), d)
Print "scalar d: " MM.ERRMSG$
d = 7
ON ERROR SKIP 1
n = Bound(x(), d)
Print "scalar 7: " MM.ERRMSG$
P a()

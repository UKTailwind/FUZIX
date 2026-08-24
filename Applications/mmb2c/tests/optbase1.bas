' OPTION BASE 1: an array holds what the program can reach, and nothing
' besides.
'
' `DIM a(3)` is FOUR elements under BASE 0 and THREE under BASE 1, and
' MMBasic allocates exactly that many either way.  We used to keep an
' unreachable element 0 under BASE 1 whatever the base said, and every
' flat walk read it: MATH(MAX) over an all-negative array answered 0,
' MATH(MEAN) divided by one too many, MATH(MEDIAN) took the median of
' one element more than existed, READ a() filled from the wrong end and
' VARADDR pointed at a slot with no name.  All of it was the phantom.
'
' Every line here is blessed against a real MMBasic 6.03.02 with
' devtools/ab.py.  The data is chosen so a phantom zero cannot hide in
' it: the reductions run over negatives, MIN runs over positives.

Option Base 1

' The reductions, on data chosen so a phantom element 0 cannot hide.
Dim Float a(3)
a(1) = -5
a(2) = -6
a(3) = -7
Print "max  "; Math(MAX a())
Print "min  "; Math(MIN a())
Print "sum  "; Math(SUM a())
Print "mean "; Math(MEAN a())
Print "sd   "; Math(SD a())
Dim Float p(3)
p(1) = 5
p(2) = 6
p(3) = 7
Print "pmin "; Math(MIN p())
Print "pmed "; Math(MEDIAN p())
Print "pmag "; Math(MAGNITUDE p())

' Storage: element 0 of the block IS a(1).
Dim Integer b
b = Peek(VarAddr p())
Print "addr "; (Peek(VarAddr p(1)) - b) / 8; (Peek(VarAddr p(3)) - b) / 8

' Two dimensions, where the phantom used to be a whole row and column.
Dim Integer m(2,3)
Dim Integer i, j, k
k = 0
For j = 1 To 3
  For i = 1 To 2
    m(i,j) = k
    k = k + 1
  Next i
Next j
b = Peek(VarAddr m())
Print "flat ";
For k = 0 To 5
  Print Peek(Integer b + k * 8);
Next k
Print
Print "bnd  "; Bound(m(), 1); Bound(m(), 2); Bound(m(), 0)

' READ fills in storage order.
Dim Integer r(2,2)
Read r()
Print "read "; r(1,1); r(2,1); r(1,2); r(2,2)
Data 10, 20, 30, 40

' A whole array through a parameter folds out of the hidden table.
Shown(m())

' The new members count what the program can reach.
Dim Float u(3), v(3)
u(1) = 3 : u(2) = 4 : u(3) = 12
Math V_Normalise u(), v()
Print "norm "; v(1); v(2); v(3)
Print "dot  "; Math(DOTPRODUCT u(), u())
Math V_Print u()
Dim Float w(2,2)
w(1,1) = 1 : w(2,1) = 2
w(1,2) = 3 : w(2,2) = 4
Math M_Print w()

Sub Shown(x%())
  Local Integer c, d
  Print "parm ";
  For d = 1 To 3
    For c = 1 To 2
      Print x%(c,d);
    Next c
  Next d
  Print
  Print "pbnd "; Bound(x%(), 1); Bound(x%(), 2)
End Sub

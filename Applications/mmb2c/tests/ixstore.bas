' Under ON ERROR SKIP a statement whose target subscript is out of range
' is abandoned: nothing is stored anywhere (FUZIX issue #9 - it landed in
' element OPTION BASE).  Below the base an array says "Dimensions", above
' it "Index out of bounds"; a structure's member array says "Index out of
' bounds" both ways.
Type T
  m(2) As Integer
End Type
Dim a%(3), s$(3), f(3), r As T
Dim k%
a%(0) = 42 : s$(0) = "keep" : f(0) = 1.5 : r.m(0) = 9

Sub Bump(v%)
  v% = v% + 1
End Sub

On Error Skip 1
a%(4) = 7
Print "1 "; a%(0); " "; MM.ErrMsg$
On Error Skip 1
a%(-1) = 7
Print "2 "; a%(0); " "; MM.ErrMsg$
On Error Skip 1
Inc a%(5)
Print "3 "; a%(0)
On Error Skip 1
Inc a%(5), 3
Print "4 "; a%(0)
On Error Skip 1
s$(5) = "x"
Print "5 "; s$(0)
On Error Skip 1
f(9) = 2.5
Print "6 "; f(0)
On Error Skip 1
Bump a%(5)
Print "7 "; a%(0)
On Error Skip 1
Read a%(6)
Print "8 "; a%(0)
k% = 5
On Error Skip 1
a%(k%) = a%(0) + 1
Print "9 "; a%(0)
On Error Skip 1
r.m(3) = 1
Print "10 "; r.m(0); " "; MM.ErrMsg$
On Error Skip 1
r.m(-1) = 1
Print "11 "; r.m(0); " "; MM.ErrMsg$
On Error Skip 1
x = a%(-2)
Print "12 "; MM.ErrMsg$
Data 5

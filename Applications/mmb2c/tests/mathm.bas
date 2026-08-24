Option Base 1
' The matrix family: M_TRANSPOSE, M_MULT, M_INVERSE, V_MULT, V_ROTATE
' and MATH(M_DETERMINANT).
'
' MMBasic's own names are kept, and getting them backwards is the one
' way to be wrong here without being told: dims[0] is the COLUMN count
' and dims[1] the row count, so an array is a(col, row) and a row is
' contiguous.  DIM p(3,2) is three wide and two tall.
'
' Every line is blessed against a real MMBasic 6.03.02 with
' devtools/ab.py.  The inverse is the reference's cofactor expansion
' rather than an LU factorisation for exactly this reason: the two do
' not round alike, and 6.123233996e-17 out of cos(90) matching on both
' sides is what says the arithmetic order is the same one.
'
' The shapes MMBasic CANNOT express are in mathm1.bas - see there.

' MMBasic names: dims[0] is the COLUMN count, dims[1] the row count, so
' a(col, row) and a row is contiguous.

' p is 3 cols x 2 rows, q is 2 cols x 3 rows, r is 2 x 2
Dim Float p(3,2), q(2,3), r(2,2)
p(1,1)=1 : p(2,1)=2 : p(3,1)=3
p(1,2)=4 : p(2,2)=5 : p(3,2)=6
q(1,1)=7 : q(2,1)=8
q(1,2)=9 : q(2,2)=10
q(1,3)=11 : q(2,3)=12
Math M_Mult p(), q(), r()
Print "mmult"; r(1,1); r(2,1); r(1,2); r(2,2)

Dim Float t(2,3)
Math M_Transpose p(), t()
Print "trans"; t(1,1); t(2,1); t(1,2); t(2,2); t(1,3); t(2,3)

Dim Float s(2,2), si(2,2)
s(1,1)=4 : s(2,1)=7
s(1,2)=2 : s(2,2)=6
Print "det  "; Math(M_DETERMINANT s())
Math M_Inverse s(), si()
Print "inv  "; si(1,1); si(2,1); si(1,2); si(2,2)

Dim Float c3(3,3), c3i(3,3)
c3(1,1)=2 : c3(2,1)=-1 : c3(3,1)=0
c3(1,2)=-1 : c3(2,2)=2 : c3(3,2)=-1
c3(1,3)=0 : c3(2,3)=-1 : c3(3,3)=2
Print "det3 "; Math(M_DETERMINANT c3())
Math M_Inverse c3(), c3i()
Print "inv3 "; c3i(1,1); c3i(2,1); c3i(3,1)
Print "     "; c3i(1,2); c3i(2,2); c3i(3,2)
Print "     "; c3i(1,3); c3i(2,3); c3i(3,3)

' V_MULT: p is 3 cols x 2 rows, so the vector is 3 long and the answer 2
Dim Float v(3), w(2)
v(1)=1 : v(2)=2 : v(3)=3
Math V_Mult p(), v(), w()
Print "vmult"; w(1); w(2)

' V_ROTATE: a unit square about its own corner, 90 degrees
Option Angle Degrees
Dim Float xs(4), ys(4), xd(4), yd(4)
xs(1)=0 : ys(1)=0
xs(2)=1 : ys(2)=0
xs(3)=1 : ys(3)=1
xs(4)=0 : ys(4)=1
Math V_Rotate 0, 0, 90, xs(), ys(), xd(), yd()
Print "rot  "; xd(1); yd(1); xd(2); yd(2)
Print "     "; xd(3); yd(3); xd(4); yd(4)
Math V_Rotate 0, 0, 90, xs(), ys(), xs(), ys()
Print "self "; xs(1); ys(1); xs(2); ys(2)
Dim Integer ix(3), iy(3), ox(3), oy(3)
ix(1)=10 : iy(1)=0
ix(2)=0  : iy(2)=10
ix(3)=-10 : iy(3)=0
Math V_Rotate 0, 0, 90, ix(), iy(), ox(), oy()
Print "roti "; ox(1); oy(1); ox(2); oy(2); ox(3); oy(3)

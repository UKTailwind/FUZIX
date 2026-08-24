Option Base 0
' The matrix family on shapes a real MMBasic cannot declare.
'
' NOT BLESSED AGAINST THE INTERPRETER, and it cannot be: MMBasic keeps
' an array's rank in the same vartbl entry as its bounds, where 0 means
' "simple variable", so no dimension can have an extent of 1 under
' either OPTION BASE.  DIM a(0) is refused under BASE 0 and DIM a(1)
' under BASE 1, both with "Dimensions", and every line below declares
' something of that shape.  It is structural there, not an oversight.
'
' We carry the rank separately, so these are honest arrays and the
' members work on them.  Two consequences worth knowing:
'
'   - a program using an extent-1 dimension will not run on a PicoMite,
'     and will fail at the DIM rather than at the MATH statement;
'   - M_INVERSE of a 1x1 is DEFINED here rather than copied.  The
'     reference's cofactor() would answer 0 for it - its inner
'     determinant of a 0x0 minor falls through to zero - which is
'     unreachable there and would be simply wrong here.
'
' The first two lines are the point of the design: a row vector times a
' column vector is a 1x1 matrix, and it agrees with MATH(DOTPRODUCT),
' which is what a program wanting the NUMBER should say.


' A row vector times a column vector: the answer is one element.
Dim Float a(2,0), b(0,2), c(0,0)
a(0,0)=1 : a(1,0)=2 : a(2,0)=3
b(0,0)=4 : b(0,1)=5 : b(0,2)=6
Math M_Mult a(), b(), c()
Print "1x1   "; c(0,0)

' the same arithmetic, as a number rather than a matrix
Dim Float u(2), v(2)
u(0)=1 : u(1)=2 : u(2)=3
v(0)=4 : v(1)=5 : v(2)=6
Print "dot   "; Math(DOTPRODUCT u(), v())

' the other way round: a column times a row is the outer product
Dim Float o(2,2)
Math M_Mult b(), a(), o()
Print "outer "; o(0,0); o(1,0); o(2,0)
Print "      "; o(0,1); o(1,1); o(2,1)
Print "      "; o(0,2); o(1,2); o(2,2)

' 1x1 determinant and inverse
Dim Float s(0,0), si(0,0)
s(0,0) = 4
Print "det1  "; Math(M_DETERMINANT s())
Math M_Inverse s(), si()
Print "inv1  "; si(0,0)

' transposing a row vector gives a column vector
Dim Float t(0,2)
Math M_Transpose a(), t()
Print "trans "; t(0,0); t(0,1); t(0,2)

' a one-row matrix by a vector: a one-element answer again
Dim Float w(0)
Math V_Mult a(), u(), w()
Print "vmult "; w(0)

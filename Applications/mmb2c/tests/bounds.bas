' A subscript inside an ON ERROR window is checked: past either end of
' any dimension is the reference's "Index out of bounds" (MMBasic.c:5003,
' 5638) - a declared array, a run-time DIM, a structure member array, an
' array parameter, and a subscript too big for 32 bits, which must not
' wrap to a small one.  Each value is set BEFORE its ON ERROR SKIP 1, so
' the skip is spent on the statement under test.  Outside a window the
' check is not compiled at all (the checks_on gating), so nothing here
' goes out of bounds unguarded - boundsig.bas has the stopping case.
Type pt
  v(2) As Integer
End Type
Dim a%(3), b(2, 4), n%, i%
Dim p As pt
n = 5
Dim d%(n)

a(3) = 7 : Print "a(3) ok "; a(3)
ON ERROR SKIP 1
a(4) = 1
Print "a(4): " MM.ERRMSG$
i = -1
ON ERROR SKIP 1
a(i) = 1
Print "a(-1): " MM.ERRMSG$

b(2, 4) = 2 : Print "b(2,4) ok "; b(2, 4)
ON ERROR SKIP 1
Print b(1, 5)
Print "b(1,5): " MM.ERRMSG$
ON ERROR SKIP 1
Print b(3, 0)
Print "b(3,0): " MM.ERRMSG$

d(5) = 3 : Print "d(5) ok "; d(5)
ON ERROR SKIP 1
d(6) = 1
Print "d(6): " MM.ERRMSG$

p.v(2) = 4 : Print "p.v(2) ok "; p.v(2)
ON ERROR SKIP 1
p.v(3) = 1
Print "p.v(3): " MM.ERRMSG$

i = 4294967297
ON ERROR SKIP 1
a(i) = 9
Print "a(2^32+1): " MM.ERRMSG$; ", a(1) is still "; a(1)

fill a()

Sub fill(q%())
  q(0) = 5 : Print "q(0) ok "; q(0)
  ON ERROR SKIP 1
  q(4) = 1
  Print "q(4): " MM.ERRMSG$
End Sub

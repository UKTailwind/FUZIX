' a() = b() - Commands.c cmd_let_arraycopy: both whole arrays of the
' same type, the same number of elements whatever the shapes (a 2x3
' fills a 6), one memcpy.  Elite copies its quaternions this way.
DIM FLOAT qa(4), qc(4), short(2)
DIM INTEGER m(1, 2), v(5)
DIM s$(2), t$(2)
DIM i
FOR i = 0 TO 4 : qa(i) = i / 2 : NEXT
qc() = qa()
FOR i = 0 TO 4 : PRINT qc(i); : NEXT : PRINT
' the first subscript is adjacent, so m() copies across in this order
m(0,0) = 1 : m(1,0) = 2 : m(0,1) = 3 : m(1,1) = 4 : m(0,2) = 5 : m(1,2) = 6
v() = m()
FOR i = 0 TO 5 : PRINT v(i); : NEXT : PRINT
s$(0) = "alpha" : s$(1) = "beta" : s$(2) = "gamma"
t$() = s$()
PRINT t$(0); " "; t$(1); " "; t$(2)
' a copy, not an alias
qa(0) = 99
PRINT qc(0)
qc() = qc()
PRINT qc(4)
On Error Skip 1
short() = qa()
Print "short    :"; MM.ErrMsg$

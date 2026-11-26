' FUZIX #45: DIM, LOCAL and STATIC take one type for the whole list as a
' bare word or after AS (cmd_dim steps over the AS), and a TYPE's name
' is a type there too.  Under OPTION DEFAULT NONE the AS forms were
' commented out.  MMBasic (a CMM2) prints the first two lines.
Option Explicit
Option Default None
Type point
  x As Integer
  y As Integer
End Type
Dim As Integer a, b(2)
a = 3 : b(1) = 4
Print a; b(1)
t
Dim As point p1, p2(1)
Dim point p3
p1.x = 1 : p2(1).y = 2 : p3.x = 3
Print p1.x; p2(1).y; p3.x
u
Sub t
  Local As Float x
  Static As String s
  x = 1.5 : s = s + "z"
  Print x; " "; s
End Sub
Sub u
  Local As point q
  Local point r
  q.x = 5 : r.y = 6
  Print q.x; r.y
End Sub

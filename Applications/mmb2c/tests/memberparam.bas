' A string member passed by reference beside a structure parameter: the
' routine writes through the parameter, but another member, or another
' structure than the member's, so the copy is exact and it translates
' (FUZIX issue #29).
Type T
  s As String
  n As Integer
End Type
Dim g As T, h As T
g.s = "a" : h.s = "z"
Sub ShowP(x$, p As T)
  p.n = p.n + 1
  Print x$; p.n
End Sub
Sub ShowQ(x$, p As T)
  p.s = "y"
  Print x$; " "; p.s
End Sub
ShowP g.s, g
ShowQ g.s, h
Print g.s; " "; h.s

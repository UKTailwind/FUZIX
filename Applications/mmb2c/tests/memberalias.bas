' A string member passed by reference to a routine that only reads it
' is a copy, which is the member exactly as long as nothing changes the
' member while the routine has it (FUZIX issue #20).  These change OTHER
' members, or another structure, so the copy is exact and they translate.
Type T
  s As String
  n As Integer
End Type
Dim g As T, h As T
g.s = "a" : h.s = "z"
Sub Show(x$)
  g.n = g.n + 1
  h.s = "y"
  Print x$; g.n; " "; h.s
End Sub
Show g.s
Show g.s
Print g.s

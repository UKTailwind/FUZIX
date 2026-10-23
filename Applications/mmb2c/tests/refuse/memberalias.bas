' REFUSE: which changes the member itself
' A string member passed by reference is a copy here, which is the member
' exactly while nothing changes it - but Show changes g.s under its own
' name while it has it, and on a PicoMite the parameter sees that (FUZIX
' issue #20).  Refused rather than printed differently.
Type T
  s As String
End Type
Dim g As T
g.s = "a"
Sub Show(x$)
  g.s = "b"
  Print x$
End Sub
Show g.s

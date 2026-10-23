' REFUSE: which changes it through 'p'
' A string member passed by reference is a copy here, which is the member
' exactly while nothing changes it - but ShowP changes it through its own
' structure parameter, which the call gave the member's structure, and
' on a PicoMite x$ and p.s are the same string (FUZIX issue #29).
Type T
  s As String
End Type
Dim g As T
g.s = "a"
ShowP g.s, g
End
Sub ShowP(x$, p As T)
  p.s = "b"
  Print x$
End Sub

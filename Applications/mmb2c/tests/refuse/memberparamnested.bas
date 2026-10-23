' REFUSE: which changes it through 'p'
' The write is made by a routine ShowP calls, given ShowP's structure
' parameter: on a PicoMite x$ and q.s are still the same string, and it
' prints b (FUZIX issue #38).  Writes made by a callee are followed.
Type T
  s As String
End Type
Dim g As T
g.s = "a"
ShowP g.s, g
End
Sub ShowP(x$, p As T)
  Inner p
  Print x$
End Sub
Sub Inner(q As T)
  q.s = "b"
End Sub

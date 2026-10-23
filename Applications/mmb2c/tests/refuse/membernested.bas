' REFUSE: which changes the member itself
' A routine Show calls changes g.s under its own name while Show has it
' (FUZIX issue #38): the callee's writes are Show's too.
Type T
  s As String
End Type
Dim g As T
g.s = "a"
Show g.s
End
Sub Show(x$)
  Change
  Print x$
End Sub
Sub Change
  g.s = "b"
End Sub

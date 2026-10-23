' REFUSE: a string member cannot be passed by reference
' Passed on by reference, a parameter is written if the one it is passed
' to is: Outer only reads n$, but hands it to SetName, which writes it.
Type rec
  name As String Length 8
End Type
Dim r As rec
Sub SetName(m$)
  m$ = "bartholomew"
End Sub
Sub Outer(n$)
  SetName n$
End Sub
r.name = "ann"
Outer r.name
Print r.name

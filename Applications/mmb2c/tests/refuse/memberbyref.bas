' REFUSE: a string member cannot be passed by reference
' A routine that writes its by-reference parameter cannot be given a
' string member: the member is its LENGTH and no more, and the parameter
' may be given 255 characters.  One that only reads it is given a copy,
' which is the member exactly (tests/membtarget).
Type rec
  name As String Length 8
End Type
Dim r As rec
Sub SetName(n$)
  n$ = "bartholomew"
End Sub
r.name = "ann"
SetName r.name
Print r.name

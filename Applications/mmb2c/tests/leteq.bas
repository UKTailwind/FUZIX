' An = inside an assigned subscript is a comparison: cmd_let finds the
' assignment's = after skipvar has stepped over the indices (PicoMite
' ad5caaf, issue 101; FUZIX issue #4).  A FOR's second = is still an
' error (tests/refuse/foreq, fortoeq).
Type P
  x As Integer
End Type
Dim a(3), s(2) As P
i = 1
a(i = 1) = 5
Print a(1)
a(Choice(i = 1, 2, 3)) = 7
Print a(2)
s(i = 1).x = 9
Print s(1).x
a(i = 2) = 4
Print a(0)

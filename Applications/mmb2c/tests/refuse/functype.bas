' REFUSE: a FUNCTION returning a TYPE is not translated yet
' The refusal was raised in the declaration pass, which swallows a raise,
' so the program translated and ran with the FUNCTION returning 0 (the
' PicoMite tree's StructParamTest, its last two tests).
Type pt
  x As Integer
  y As Integer
End Type
Dim p As pt
Function MakePt(a, b) As pt
  MakePt.x = a
  MakePt.y = b
End Function
p = MakePt(100, 200)
Print p.x; p.y

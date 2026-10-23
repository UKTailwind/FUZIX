' REFUSE: whole arrays of structures as parameters are not translated yet
' The refusal was raised in the declaration pass, which swallows a raise,
' so the routine lost its parameters and the program ran without them
' (the PicoMite tree's StructParamTest, its array tests).
Type pt
  x As Integer
End Type
Dim pts(3) As pt
Sub Wipe(a() As pt)
  a(1).x = 0
End Sub
pts(1).x = 5
Wipe pts()
Print pts(1).x

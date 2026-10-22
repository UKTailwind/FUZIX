' REFUSE: A sub/fun has the same name: SCALE
' A parameter is made by findvar when the routine is called, so it cannot
' have a FUNCTION's name (48b4680).
Function Scale(v)
  Scale = v * 2
End Function
Sub Report(scale)
  Print scale
End Sub
Report 3

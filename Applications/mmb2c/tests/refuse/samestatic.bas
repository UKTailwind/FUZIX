' REFUSE: A sub/fun has the same name: COUNT
' STATIC's local alias is made by findvar too (StaticLink), so it cannot
' take a FUNCTION's name either (48b4680).
Function Count()
  Count = 1
End Function
Sub Tick
  Static count
  count = count + 1
End Sub
Tick

' REFUSE: A sub/fun has the same name: TOTAL
' findvar will not make a variable with a FUNCTION's name (48b4680):
' here a LOCAL in another routine.
Function Total(a)
  Total = a * 2
End Function
Sub Work
  Local total
  total = 3
End Sub
Work

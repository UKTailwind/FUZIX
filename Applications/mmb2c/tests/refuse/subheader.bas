' REFUSE: return type conflicts with the name suffix
' Only the declaration pass reads a SUB or FUNCTION line, and it
' swallowed what it raised: the FUNCTION kept the wrong type and the
' program translated with rc 0.
Function Tag$(a) As Integer
  Tag$ = "x"
End Function
Print Tag$(1)

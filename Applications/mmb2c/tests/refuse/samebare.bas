' REFUSE: A sub/fun has the same name: NEXTVAL
' getvalue calls a FUNCTION only with a '(' after its name
' (MMBasic.c:4259); without one the name is a variable, which findvar
' will not make (48b4680).
Function NextVal()
  NextVal = 7
End Function
x = NextVal
Print x

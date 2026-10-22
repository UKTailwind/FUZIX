' REFUSE: A sub/fun has the same name: GREET
' CALL takes the SUB's name as a string: a bare name is a variable to
' getCstring, and a variable cannot share the SUB's name (48b4680).
Sub Greet(n)
  Print "greet"; n
End Sub
Call Greet, 5

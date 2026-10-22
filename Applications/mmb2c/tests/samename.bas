' Names that are not a SUB or FUNCTION's own, which 48b4680 still
' accepts: a prefix or an extension of one, a FUNCTION's result inside
' it, and a FUNCTION called with its brackets - also as the name CALL
' runs.  Run on a real PicoMite: the output must be identical.
Sub Show(n)
  Print "show"; n
End Sub
Function Twice(n)
  Twice = n * 2
  Twice = Twice + 1
End Function
Function Which$()
  Which$ = "Show"
End Function
shows = 1
sho = 2
twic = 3
Show shows + sho
Print Twice(twic)
Call Which$(), 9
Print Which$() + "!"

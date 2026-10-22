' A FUNCTION is not a command, and CALL takes a SUB only (edc7999): each
' stops at the line, before any argument is evaluated.  Run on a real
' PicoMite: the output must be identical.
Function F(a)
  Print "F ran"
  F = a
End Function
Function T(a)
  Print "argument evaluated"
  T = a
End Function
Sub S(a)
  Print "S"; a
End Sub
ON ERROR SKIP 1
F T(1)
Print "statement: " MM.ERRMSG$
ON ERROR SKIP 1
F T(1), q = 2
Print "with =: " MM.ERRMSG$
ON ERROR SKIP 1
Call "F", T(1)
Print "call: " MM.ERRMSG$
ON ERROR SKIP 1
Call "Nowhere", T(1)
Print "unknown: " MM.ERRMSG$
n$ = "F"
ON ERROR SKIP 1
Call n$, 1
Print "by name: " MM.ERRMSG$
Call "S", 6

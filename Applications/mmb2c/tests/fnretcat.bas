' A FUNCTION's own name is a writable variable: CAT and INC target the
' return value, not an invisible implied global of the same name.
' Found by PETSCII Robots' path$(), which builds its result with
' `Cat path$, "/" + f$`.  Run on a real PicoMite: the output must be
' identical.
Option Explicit
Option Default Integer

Function build$(f$)
  build$ = "base"
  Cat build$, "/" + f$
End Function

Function count(n)
  count = 10
  Inc count, n
End Function

Print build$("file")
Print count(5)

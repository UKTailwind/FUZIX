' CHOICE as fun_ternary: only the chosen branch is evaluated, any non-zero
' condition is true, and a string answer is a COPY (a121e4e, issue #84) -
' passed to a SUB that changes its parameter, it must leave the variable
' it came from alone.
Dim n%
Sub Change(s$)
  s$ = "changed"
End Sub
Function F$(x$)
  n% = n% + 1 : F$ = x$
End Function
b$ = "orig" : c$ = "other"
Change Choice(1, b$, c$)
Print "copy: "; b$
Print Choice(0, b$, c$); " "; Choice(0.5, "y", "n"); " "; Choice(-1, 1, 2)
n% = 0
Print Choice(1, F$("a"), F$("b")); n%

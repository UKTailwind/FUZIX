' TRIM$'s where$ as fun_trim takes it (1ee2feb, issue #76): L, R or B as
' a bare word or as the first letter of a string, either case; anything
' else is "Invalid syntax".  An empty mask slot keeps the space.
Dim n%
Function W$()
  n% = n% + 1 : W$ = "R"
End Function
s$ = "  xx  "
Print "[" + Trim$(s$) + "]"
Print "[" + Trim$(s$, " ", R) + "]"
Print "[" + Trim$(s$, " ", b) + "]"
Print "[" + Trim$(s$, " ", "b") + "]"
Print "[" + Trim$(s$, " ", "right") + "]"
Print "[" + Trim$(s$, , "B") + "]"
Print "[" + Trim$("**a**", "*", "B") + "]"
Print "[" + Trim$(s$, " ", W$()) + "]"; n%
bad$ = "x"
ON ERROR SKIP 1
t$ = Trim$(s$, " ", bad$)
Print "bad: " MM.ERRMSG$

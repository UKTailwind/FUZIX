' DIM's LENGTH takes an expression (PicoMite #104).  An array's must be
' known at translation, so a CONST will do; a scalar's changes nothing
' here, so anything goes.
Const W = 8
Dim a$(3) Length W
Dim b$(3) Length 8
a$(1) = "abc"
Print a$(1); Len(a$(1))
Print Peek(VarAddr a$(1)) - Peek(VarAddr a$(0)); Peek(VarAddr b$(1)) - Peek(VarAddr b$(0))
Dim c$ Length Len("it's")
c$ = "hello"
wide = 1
Dim d$ Length Choice(wide = 1, 80, 40)
d$ = "x"
Print c$; d$

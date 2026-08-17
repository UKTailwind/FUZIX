' A global CONST whose expression is not a compile-time constant is
' evaluated ONCE, where the CONST statement stands - cmd_const runs
' DoExpression at the statement and stores the VALUE.  The old #define
' form re-ran the expression at every use: robots' LCD_DISPLAY called
' Mm.Device$ twice per test and leaked its scratch strings until the
' pool died in fade_in.  Run on a real PicoMite: the output must be
' identical.
Option Explicit
Option Default Integer

Dim calls

Function probe(n)
  calls = calls + 1
  probe = n * 10
End Function

Const K = probe(3)
Const L$ = Choice(K = 30, "yes", "no")
Const M = K + 5

Dim i, x
For i = 1 To 50
  x = K + Len(L$) + M
Next i
Print K; " "; L$; " "; M; " x="; x; " calls="; calls

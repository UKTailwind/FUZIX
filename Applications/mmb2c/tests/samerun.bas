' A variable with a SUB or FUNCTION's name fails when its line runs, as
' findvar's check does there (FUZIX issue #20): a bare FUNCTION name, a
' name given to CALL, DIM, an implied variable, LOCAL, STATIC and CONST.
Function NextVal()
  NextVal = 7
End Function
Sub Greet(n)
  Print "greet"; n
End Sub
Sub Show
  Print "show"
End Sub
Sub Ding
End Sub
Function Total(a)
  Total = a * 2
End Function
Sub Work
  Local total
  total = 3
End Sub
Function Count()
  Count = 1
End Function
Sub Tick
  Static count
  count = count + 1
End Sub
Sub Limit
End Sub
Print "start"
On Error Ignore
x = NextVal
Print "bare: "; MM.ErrMsg$
On Error Clear
Call Greet, 5
Print "call: "; MM.ErrMsg$
On Error Clear
Dim show$
Print "dim: "; MM.ErrMsg$
On Error Clear
ding = 1
Print "implied: "; MM.ErrMsg$
On Error Clear
Work
Print "local: "; MM.ErrMsg$
On Error Clear
Tick
Print "static: "; MM.ErrMsg$
On Error Clear
Const limit = 10
Print "const: "; MM.ErrMsg$
Show
Print Total(2); NextVal()

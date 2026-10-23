' Under OPTION EXPLICIT findvar says "NAME is not declared" before it
' looks for a SUB or FUNCTION of the name, and both when the line runs
' (FUZIX issue #20).  A DIM is no use of an undeclared name.
Option Explicit
Function NextVal()
  NextVal = 7
End Function
Dim x
On Error Ignore
x = NextVal
Print "1 "; MM.ErrMsg$
On Error Clear
y = 1
Print "2 "; MM.ErrMsg$
On Error Clear
Dim nextval
Print "3 "; MM.ErrMsg$
On Error Clear
Print "4 "; NextVal()

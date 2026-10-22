' A label may share a SUB's name: the tokeniser makes a label of a name
' with its ':' straight after it, first on the line, whatever else has
' the name (MMBasic.c:2292-2306; 2d5307e, the reference's label8.py).
' With a space before the ':' it is a statement - here a call.  A jump
' to a name that is not a label is "Cannot find label" when it runs,
' and to a missing line number "Line number".
Sub Here
  Print "in SUB"
End Sub
Sub Ping
  Print "ping"
End Sub
Gosub Here
Print "after"
Here
Restore Here
Read v
Print "read"; v
Ping : Ping
ON ERROR SKIP 1
Goto Ping
Print "goto a SUB: " MM.ERRMSG$
ON ERROR SKIP 1
Gosub Nowhere
Print "gosub nowhere: " MM.ERRMSG$
ON ERROR SKIP 1
Restore Nowhere
Print "restore nowhere: " MM.ERRMSG$
ON ERROR SKIP 1
Goto 9999
Print "goto 9999: " MM.ERRMSG$
End
Here:
Print "at label"
Return
Data 42

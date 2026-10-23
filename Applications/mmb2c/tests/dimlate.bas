' A DIM with several names, one of which fails as the line runs: the names
' before it are made, as findvar makes them one at a time (FUZIX issue
' #23).  Cutting the whole DIM left d() declared and never allocated -
' the program crashed - and s$ without its initialiser.
On Error Ignore
n = 10
Dim d(n), abcdefghijklmnopqrstuvwxyz012345(3)
d(5) = 8
Print d(5)
Dim s$ = "x", abcdefghijklmnopqrstuvwxyz012345q = 1
Print "[" + s$ + "]"
Dim e(n), show
e(2) = 4
Print e(2)
End
Sub Show
End Sub

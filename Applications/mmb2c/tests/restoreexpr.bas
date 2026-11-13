' RESTORE takes an expression (PicoMite #103): a string names a label, in
' any case, a number a line; a bare name is a label first and a variable
' second.
Dim l$, n
Restore Two
Read a : Print a
l$ = "ONE"
Restore l$
Read a : Print a
Restore Choice(1 = 2, "one", "two")
Read a : Print a
n = 300
Restore n
Read a : Print a
Restore n - 200
Read a : Print a
show
End
Sub show
  Local k$ = "two"
  Restore k$
  Read b : Print b
End Sub
One:
Data 1
Two:
Data 2
100 Data 100
300 Data 300

' END SUB and END FUNCTION after a ':' or a label end the routine, so a
' /* block after them is at the top level and allowed (FUZIX #33, the
' PicoMite session's review of it)
foo
bar
Print baz(2)
lbl2
End
Sub foo
  Print "a" : End Sub
/*
c
*/
Sub bar : Print "b" : End Sub
/*
d
*/
Function baz(x)
  baz = x * 2 : End Function
/*
e
*/
Sub lbl2
  Print "l"
done: End Sub
/*
f
*/

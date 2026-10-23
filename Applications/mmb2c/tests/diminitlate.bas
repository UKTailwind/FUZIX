' cmd_dim makes the array and then sets each value in turn, so a late
' error in one of the values keeps the array and the values before it
' (FUZIX issue #32): the program crashed, the array never allocated.  A
' STATIC's values are set when it is made, so on the second call there
' is no error and its values stay.
On Error Ignore
n = 3
Dim e(n) = (1, 2, show, 4)
e(1) = 7
Print e(0); e(1); e(3)
Dim f(3) = (5, show, 6, 7)
Print f(0); f(1)
Loc
Loc
Print "end"
End
Sub Show
End Sub
Sub Loc
  Local v(n) = (1, show)
  v(1) = 9
  Print v(0); v(1)
  Static w(2) = (3, show, 5)
  Print w(0); w(2)
End Sub

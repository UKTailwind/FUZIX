' ON n GOTO / GOSUB to a label that is not there is an error only when
' that target is chosen, as findlabel's is (FUZIX issue #20).
n = 1
On n GoTo one, nowhere
one:
Print "one"
On n GoSub sub1, nowhere, 99
Print "after gosub"
n = 2
On Error Skip 1
On n GoTo one2, nowhere
Print MM.ErrMsg$
n = 3
On Error Skip 1
On n GoSub sub1, nowhere, 99
Print MM.ErrMsg$
End
one2:
Print "not reached"
sub1:
Print "sub1"
Return

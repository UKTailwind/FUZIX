' A FUNCTION called inside an ON ERROR SKIP window prints as it goes, and
' an END in it keeps what it printed (FUZIX issue #21).  The SKIP count
' stays armed through the body - it is spent only at the depth that set
' it - but the body has no statement guards, so output held for one was
' kept to the FUNCTION's exit, and lost at an END inside it.
On Error Skip
y = f()
Print "after"
End
Function f()
  Print "bye"
  End
End Function

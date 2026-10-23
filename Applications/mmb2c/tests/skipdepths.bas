' A SUB's statements still use up ON ERROR SKIP's count: its body runs
' in its caller's loop (PicoMite issue 34 changed only FUNCTIONs).
' SKIP 5 is 6: the ON ERROR line leaves 5, the call 4, t = 1 3, t = 2
' 2, END SUB 1 - so the first ERROR after s is skipped and the second
' is not.  The count is a variable, so every statement is counted.
Sub s
  t = 1
  t = 2
End Sub

n = 5
On Error Skip n
s
Error "one"
Print "after one: "; MM.ErrMsg$
Error "two"
Print "not reached"

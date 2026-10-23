' ON ERROR SKIP 1 then a single-line IF: the IF is the statement the
' count is spent on, true or false.  When its THEN statement took the
' compile-time window the IF had no guard, so with the test false the
' count was never spent, and a later error with no ON ERROR of its own
' was skipped - and its poison suppressed everything after it, PRINTs
' included.  MMbg stopped drawing at "Loading neural nets" that way.
k = 0 : ymch = 0
For i = 1 To 3
  On Error Skip 1
  If k = 2 And ymch = 0 Then ymch = i
Next i
Print "loop done"
a$ = String$(250, "x")
a$ = a$ + "0123456789"
Print "not reached"

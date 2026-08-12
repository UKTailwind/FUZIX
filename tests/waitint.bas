' PAUSE with something armed must SERVICE it - and must TERMINATE.
'
' The second half is the point of this test.  A PAUSE for a program with
' an interrupt is not one sleep but a loop of short ones with the poll
' between them, and the first version of that loop waited on a deadline
' taken from mm_us.  In a plain host build mm_us is clock(), which counts
' processor time and does not advance while the process sleeps, so the
' loop slept, saw no progress, and slept again for ever.  Nothing caught
' it: settick.bas arms a tick but never pauses.
'
' HOW MANY times the handler runs is deliberately not printed.  It
' depends on whether the clock is real - ten times under fcc and on the
' board, none in a plain build where sleeping costs no processor time -
' and cgate compares those two outputs against each other.  The count
' belongs on the board; what belongs here is that this program ends.
Dim integer n, i

SetTick 50, tock, 1
Pause 300
SetTick 0, tock, 1
Print "tick armed:  PAUSE returned"

' the same for a pin interrupt, which asks for a different slice
SetPin 1, DIn
SetPin 1, IntB, pinint
Pause 200
SetPin 1, Off
Print "pin armed:   PAUSE returned"

' and nested: a PAUSE inside a handler is still a PAUSE
SetTick 50, slowtock, 1
Pause 300
SetTick 0, slowtock, 1
Print "handler paused too: PAUSE returned"

Print "all waits terminated"
End

Sub tock
  n = n + 1
End Sub

Sub pinint
  n = n + 1
End Sub

Sub slowtock
  Pause 10
End Sub

' threrr8.bas - THREAD: an error in a thread ends the program, names the
' thread, and lists where each thread was (no line numbers: PLAN-threads.md D5)
Option Explicit
Dim Integer go
Sub Napper
  Pause 100000
End Sub
Sub Failer
  Thread Lock 2
  Error "this thread has failed"
End Sub
Thread Priority 0, 2
Thread Start 1, Napper
Thread Start 3, Failer
Thread Wait 3
Print "not reached"

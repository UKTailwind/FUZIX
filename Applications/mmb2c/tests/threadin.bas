' threadin.bas - THREAD: INPUT and LINE INPUT in a program with threads.
' Typed input lets the other threads run meanwhile (mmx_io_wait); from a
' file it is there at once, so this checks the lines and the threads after.
Option Explicit
Dim Integer n, ticks
Dim a$, b$
Sub Busy
  Do : Inc n : Loop
End Sub
Sub Tick
  Inc ticks
End Sub
Sub Ticker
  SetTick 5, Tick, 1
  Do : Pause 1000 : Loop
End Sub
Thread Start 1, Busy
Thread Start 2, Ticker
Input "name"; a$
Line Input b$
Print a$; " / "; b$
Pause 30
Print "the threads ran:"; n > 0; ticks > 0
Thread Stop 1 : Thread Stop 2

' Does a SETTICK handler run DURING a PAUSE?
'
' MMBasic's cmd_pause checks interrupts every time round its wait loop,
' so it does.  Ours could not: PAUSE was one sleep and the poll sites
' are between statements.  A program whose main loop is PAUSE - which is
' most programs that arm a tick - never saw its handler at all.
'
' Run this on the board, where the clock is real.  Ten ticks of 50 ms
' should fit inside a PAUSE of 500.
Dim integer n, t

SetTick 50, tock, 1
t = Timer
Pause 500
t = Timer - t
SetTick 0, tock, 1

Print "PAUSE 500 with SETTICK 50"
Print "  elapsed  "; Str$(t, 0, 0); " ms";
If t >= 450 And t <= 700 Then Print "   ok" Else Print "   WRONG"
Print "  handler ran "; Str$(n, 0, 0); " times";
If n >= 8 And n <= 12 Then Print "   ok" Else Print "   WRONG (want about 10)"

' and a slow tick must not turn the PAUSE into a spin
n = 0
SetTick 1000, tock, 1
t = Timer
Pause 300
t = Timer - t
SetTick 0, tock, 1
Print "PAUSE 300 with SETTICK 1000"
Print "  elapsed  "; Str$(t, 0, 0); " ms";
If t >= 250 And t <= 500 Then Print "   ok" Else Print "   WRONG"
Print "  handler ran "; Str$(n, 0, 0); " times (0 expected)"
End

Sub tock
  n = n + 1
End Sub

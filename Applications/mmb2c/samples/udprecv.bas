Option EXPLICIT
Dim Integer n
WEB UDP SERVER PORT 5000
WEB UDP INTERRUPT rx
Print "listening on 5000"
Timer = 0
Do While n < 5 And Timer < 30000
  Pause 20
Loop
Print "done: "; n
End

Sub rx
  n = n + 1
  Print "from "; MM.ADDRESS$; ": "; MM.MESSAGE$
  WEB UDP SEND MM.ADDRESS$, 5001, "ack " + Str$(n)
End Sub

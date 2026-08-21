Option EXPLICIT
Dim Integer i, n
Dim String peer
peer = MM.CMDLINE$
If peer = "" Then Print "usage: udpsend ip" : End
WEB UDP SERVER PORT 5001
WEB UDP INTERRUPT rx
For i = 1 To 5
  WEB UDP SEND peer, 5000, "ping " + Str$(i)
  Pause 300
Next i
Pause 500
Print "acks: "; n
End

Sub rx
  n = n + 1
  Print MM.MESSAGE$
End Sub

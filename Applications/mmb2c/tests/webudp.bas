' webudp.bas - WEB UDP stage 1 (PLAN-web.md §11), as a loopback:
' bind the server port, arm the interrupt, send to ourselves, and
' read the datagram back through MM.MESSAGE$/MM.ADDRESS$.
Option EXPLICIT

Dim Integer got
Dim String m, a

WEB UDP SERVER PORT 47999
WEB UDP INTERRUPT rx
WEB UDP SEND "127.0.0.1", 47999, "hello udp"

Timer = 0
Do While got = 0 And Timer < 3000
  Pause 5
Loop

If got Then
  Print "msg: "; m
  Print "addr: "; a
Else
  Print "timeout"
EndIf

' a second datagram after the first was consumed: the buffer must
' hold the most recent one
WEB UDP SEND "127.0.0.1", 47999, "second"
got = 0
Timer = 0
Do While got = 0 And Timer < 3000
  Pause 5
Loop
If got Then Print "msg: "; m Else Print "timeout"

' interrupt off: a further send must not fire the handler
WEB UDP INTERRUPT 0
WEB UDP SEND "127.0.0.1", 47999, "third"
got = 0
Pause 100
Print "fired: "; got
' ... but the reader still sees the latest datagram, as the WebMite's
' buffer does with no interrupt armed
Print "msg: "; MM.MESSAGE$
End

Sub rx
  m = MM.MESSAGE$
  a = MM.ADDRESS$
  got = 1
End Sub

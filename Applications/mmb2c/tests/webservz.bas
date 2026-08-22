Option EXPLICIT
Dim Integer req(64)
req(0) = 99 : req(5) = 42
WEB TCP SERVER PORT 48123
WEB TCP INTERRUPT rx
WEB TCP READ 3, req()
Print "len: "; req(0); " probe: "; req(5)
Print "maxconn: "; MM.Info(MAX CONNECTIONS)
Print "ip: "; MM.Info(IP ADDRESS)
WEB TCP CLOSE 3
WEB TCP INTERRUPT 0
End
Sub rx
End Sub

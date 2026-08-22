Option EXPLICIT
Dim Integer n
WEB TCP SERVER PORT 8080
WEB TCP INTERRUPT handler
Print "serving on 8080, ip "; MM.Info(IP ADDRESS)
Timer = 0
Do While Timer < 60000
  Pause 20
Loop
Print "served "; n; " requests"
End

Sub handler
  Local Integer a, b(512)
  For a = 1 To MM.Info(MAX CONNECTIONS)
    WEB TCP READ a, b()
    If LLen(b()) > 0 Then
      n = n + 1
      If LInStr(b(), "GET /hello.txt") > 0 Then
        WEB TRANSMIT FILE a, "hello.txt", "text/plain"
      ElseIf LInStr(b(), "GET /") > 0 Then
        WEB TRANSMIT CODE a, 204
      Else
        WEB TRANSMIT CODE a, 404
      EndIf
    EndIf
  Next a
End Sub

Option EXPLICIT
Dim Integer n
Dim Float t
Dim String s
n = 7 : t = 3.5 : s = "x"
serve 3
Print "compiled ok"
End

Sub serve pnbr As Integer
  Local Integer loc
  loc = pnbr * 2
  If n = -999 Then
    WEB TCP SERVER PORT 48124
    WEB TRANSMIT PAGE 1, "webpage.html"
  EndIf
End Sub

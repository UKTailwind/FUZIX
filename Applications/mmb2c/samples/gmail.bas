' gmail.bas - send a mail through Gmail from compiled BASIC.
' PLAN-web.md S12.4: implicit TLS on 465, AUTH LOGIN with an App
' Password.  Credentials come from ./gmail.conf - three lines:
'   your.address@gmail.com
'   destination@wherever
'   the16charapppassword
' so they live on the card, not in any program or transcript.
'
' The Pause after OPEN lets Gmail's 220 greeting arrive and queue;
' the first REQUEST then discards it (the WebMite drops it the same
' way, for its own reason) so EHLO gets EHLO's answer.
Option EXPLICIT
Const cr = Chr$(13) + Chr$(10)
Dim Integer b(512)
Dim String mfrom, mto, pass

Open "gmail.conf" For Input As #1
Line Input #1, mfrom
Line Input #1, mto
Line Input #1, pass
Close #1

WEB TLS CA "/etc/ca.pem"
WEB OPEN TLS CLIENT "smtp.gmail.com", 465, 20000
Pause 300
WEB TCP CLIENT REQUEST "EHLO pc3" + cr, b()
WEB TCP CLIENT REQUEST "AUTH LOGIN" + cr, b()
WEB TCP CLIENT REQUEST b64$(mfrom) + cr, b()
WEB TCP CLIENT REQUEST b64$(pass) + cr, b()
WEB TCP CLIENT REQUEST "MAIL FROM:<" + mfrom + ">" + cr, b()
WEB TCP CLIENT REQUEST "RCPT TO:<" + mto + ">" + cr, b()
WEB TCP CLIENT REQUEST "DATA" + cr, b()
WEB TCP CLIENT REQUEST "From: " + mfrom + cr + "To: " + mto + cr + "Subject: PC3 stage 3" + cr + cr + "Sent from compiled BASIC over TLS on the PC3." + cr + "." + cr, b()
Print "final: "; LGetStr$(b(), 1, LLen(b()))
If LInStr(b(), "250 2.0.0") > 0 Then Print "MAIL SENT" Else Print "not confirmed - read the reply"
WEB CLOSE TCP CLIENT
End

' retic.bas's own pure-BASIC base64 (the commented-out original)
Function b64$(si As String)
  Local String cs = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
  Local Integer i, p, n, pad
  For p = 1 To Len(si) Step 3
    n = Asc(Mid$(si, p, 1)) << 8
    If p + 1 <= Len(si) Then n = n Or Asc(Mid$(si, p + 1, 1)) Else pad = pad + 1
    n = (n << 8)
    If p + 2 <= Len(si) Then n = n Or Asc(Mid$(si, p + 2, 1)) Else pad = pad + 1
    For i = 3 To 0 Step -1
      b64$ = b64$ + Mid$(cs, ((n >> (i * 6)) And &B111111) + 1, 1)
    Next i
  Next p
  b64$ = Left$(b64$, Len(b64$) - pad) + Left$("==", pad)
End Function

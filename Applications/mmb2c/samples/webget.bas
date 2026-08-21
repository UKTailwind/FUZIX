Option EXPLICIT
Const cr = Chr$(13) + Chr$(10)
Dim Integer b(512)
Dim String host
host = MM.CMDLINE$
If host = "" Then Print "usage: webget ip" : End
WEB OPEN TCP CLIENT host, 8080
WEB TCP CLIENT REQUEST "GET /hello.txt HTTP/1.0" + cr + cr, b()
Print "got "; LLen(b()); " bytes"
Print "status: "; LGetStr$(b(), 1, 15)
If LInStr(b(), "squirrels") > 0 Then Print "body ok" Else Print "BODY MISSING"
WEB CLOSE TCP CLIENT

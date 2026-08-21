Option EXPLICIT
Const cr = Chr$(13) + Chr$(10)
Dim Integer b(1024)
WEB OPEN TCP CLIENT "example.com", 80
WEB TCP CLIENT REQUEST "GET / HTTP/1.0" + cr + "Host: example.com" + cr + cr, b()
Print "got "; LLen(b()); " bytes"
Print "status: "; LGetStr$(b(), 1, 15)
WEB CLOSE TCP CLIENT

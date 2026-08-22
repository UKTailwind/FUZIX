Option EXPLICIT
Const cr = Chr$(13) + Chr$(10)
Dim Integer b(1024)
WEB TLS CA "/etc/ca.pem"
WEB OPEN TLS CLIENT "www.google.com", 443, 20000
WEB TCP CLIENT REQUEST "GET / HTTP/1.0" + cr + "Host: www.google.com" + cr + cr, b()
Print "got "; LLen(b()); " bytes"
Print "status: "; LGetStr$(b(), 1, 15)
WEB CLOSE TCP CLIENT

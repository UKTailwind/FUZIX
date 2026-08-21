Option EXPLICIT
Dim Integer b(64)
b(0) = 0
' a closed port: the open must time out and error
WEB OPEN TCP CLIENT "127.0.0.1", 9, 100
Print "unreached"

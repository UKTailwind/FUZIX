' Nothing on a connection works before WEB TCP SERVER PORT: the
' reference says "Server not open" (MMtcpserver.c:831 and 894).  The
' slot table here started zero-filled, and -1 is its free marker, so
' CLOSE closed stdin and the writes went to fd 0.
Dim Integer b(8)
b(0) = 2 : b(1) = &H6968

ON ERROR SKIP 1
WEB TCP CLOSE 1
PRINT "close: " MM.ERRMSG$

ON ERROR SKIP 1
WEB TCP SEND 1, b()
PRINT "send: " MM.ERRMSG$

ON ERROR SKIP 1
WEB TRANSMIT CODE 1, 404
PRINT "code: " MM.ERRMSG$

ON ERROR SKIP 1
WEB TRANSMIT FILE 1, "x.html", "text/html"
PRINT "file: " MM.ERRMSG$

' TRANSMIT PAGE has the same check, but its page is read at translate
' time and the corpus carries no page files, so it is not tested here.

' a READ has nothing to give, and says so with an empty array
WEB TCP READ 1, b()
PRINT "read: " b(0)

' WEB NTP / PING / CONNECT - stage 7.  A compile gate in the
' webpage.bas manner: the guard never fires, so every form is
' translated, compiled and linked but never run - the host has no
' radio, and ntpdate/ping/wifi live on the board.
Dim ok%
ok% = 0
If ok% <> 0 Then
  WEB NTP
  WEB NTP 2
  WEB NTP -9.5, "time.example.com"
  WEB NTP 1, "x", 8000
  WEB PING "192.168.1.1"
  WEB PING "host.example.com", 2
  WEB CONNECT
  WEB CONNECT "myssid", "mykey"
End If
Print "compiled"
End

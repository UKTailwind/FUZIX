' Stage-7 board smoke: UPTIME, the CONNECT gate, PING, NTP.
' Needs the radio joined (it is, from /etc/wifi.conf at boot).
Print "uptime:"; Mm.Info(UPTIME)
WEB CONNECT
Print "link up"
WEB PING "192.168.1.79", 2  ' the other PC3; the PC firewalls ICMP
WEB NTP 1
Print "clock set to UTC+1"
End

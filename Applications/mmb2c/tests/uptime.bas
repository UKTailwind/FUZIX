' MM.INFO(UPTIME) - seconds since boot as a FLOAT (the reference is
' time_us_64()/1000000.0).  Time-varying, so the pin is shape, not
' value.
Dim t As Float
t = Mm.Info(UPTIME)
If t > 0 Then Print "positive"
Pause 20
If Mm.Info(UPTIME) > t Then Print "advances"
End

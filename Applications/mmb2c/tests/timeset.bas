' Setting the clock without ever reading it: the setters are in
' mmb_datetime.h, which only a read used to bring in, so this did not
' compile ("implicit declaration of mm_set_time").
Time$ = "10:00:00"
Date$ = "02-03-2026"
Print "set"

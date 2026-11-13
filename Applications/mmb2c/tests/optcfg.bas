' OPTION PICOMITE is nothing (PicoMite 1615889); OPTION MILLISECONDS and
' OPTION PRINTSCREEN are warned about and ignored.  TIME$ = n moves the
' time of day by n seconds and keeps the date, wrapping at midnight
' (cmd_time's numeric form, PicoMite 4a506b0).
Option PicoMite
Option Picomite VGA
Option Milliseconds On
Option PrintScreen Format PNG
Date$ = "01-01-2026"
Time$ = "10:00:00"
Time$ = 3600
Print Left$(Time$, 5)
Time$ = "23:59:00"
Time$ = 120
Print Left$(Time$, 5); " "; Date$
Time$ = "00:00:30"
Time$ = -60
Print Left$(Time$, 5); " "; Date$

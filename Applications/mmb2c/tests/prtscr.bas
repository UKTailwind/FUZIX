' OPTION PRINTSCREEN ON, OFF and FORMAT translate and run, and the
' program goes on as before: the poll that looks for Print Screen costs
' nothing a program can see.  The key itself is pc3host's prtscr test.
Option PrintScreen Format PNG
Option PrintScreen On
For i = 1 To 3
  Print i;
Next
Print
Pause 20
Print Len(Inkey$)
Option PrintScreen Off
Option PrintScreen Format BMP
Option PrintScreen On
Print "done"

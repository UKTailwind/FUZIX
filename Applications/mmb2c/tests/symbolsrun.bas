' OPTION SYMBOLS OFF below the top of the program is an error only when it
' is executed (FUZIX issue #20): IF 0 THEN OPTION SYMBOLS OFF runs.
Print "first"
If 0 Then Option Symbols Off
Print "second"
Option Symbols Off
Print "not reached"

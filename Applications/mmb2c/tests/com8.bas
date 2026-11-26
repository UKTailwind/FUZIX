REM OPEN comspec$ AS #n, a serial port (Serial.c SerialOpen).  On a PC the
REM port is named as the OS names it, COM3 or /dev/ttyUSB0; this runtime
REM has none and says so.  An interrupt named in a literal spec is armed
REM by the translator; in a spec worked out as the program runs it cannot
REM be.  pc3host runs tests/e2e/serial.bas on a pseudo-terminal.
On Error Skip
Open "COM1:9600,256,rx,4,EVEN,S2" As #1
Print MM.ErrMsg$
p$ = "COM2:115200"
On Error Skip
Open p$ As #3
Print MM.ErrMsg$
On Error Skip
Open p$ + ",,rx" As #3
Print MM.ErrMsg$
REM LIST SERIAL PORTS names a PC's ports by the names OPEN takes
On Error Skip
LIST SERIAL PORTS
Print MM.ErrMsg$
End

Sub rx
End Sub

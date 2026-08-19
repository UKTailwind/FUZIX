' ONEWIRE and TEMPR - what the gates can check.
'
' There is no bus and no sensor here, so this is about the forms: that
' both translators agree on the C, that it compiles, and that ONEWIRE's
' data arguments are the SAME ones I2C and SPI take.  They are the same
' code - MMBasic's owWrite and owRead call GetCommsTxData and
' GetCommsRxDest exactly as I2C.c and SPI.c do - so if i2c2.bas and
' spi.bas pass, these forms work too.
'
' The hardware half lives in samples/tempr.bas, which runs against a
' DS18B20 on GP26 and checks the timing.
Dim integer b(8), v1, v2, v3
Dim float g(8)
Dim s$
Dim integer ls(20)

ONEWIRE RESET 26
PRINT "reset done, MM.ONEWIRE = ";MM.ONEWIRE

' every TX form
ONEWIRE WRITE 26, 1, 1, &H33            ' a list
ONEWIRE WRITE 26, 0, 3, b()             ' an integer array
ONEWIRE WRITE 26, 0, 3, g()             ' a float array
s$ = "abc"
ONEWIRE WRITE 26, 0, 3, s$              ' a string
LONGSTRING CLEAR ls()
LONGSTRING APPEND ls(), "abc"
ONEWIRE WRITE 26, 0, 3, LONGSTRING ls() ' a long string
PRINT "tx forms translated"

' every RX form
ONEWIRE READ 26, 0, 3, b()
ONEWIRE READ 26, 0, 3, g()
ONEWIRE READ 26, 0, 3, s$
ONEWIRE READ 26, 0, 3, v1, v2, v3
ONEWIRE READ 26, 0, 1, v1
ONEWIRE READ 26, 0, 3, LONGSTRING ls()
PRINT "rx forms translated"

' the flag bits, which are MMBasic's
ONEWIRE WRITE 26, 2, 1, &HCC            ' reset afterwards
ONEWIRE WRITE 26, 4, 1, 1               ' one bit only
ONEWIRE WRITE 26, 8, 1, &H44            ' strong pull-up after
PRINT "flag bits translated"

' the shared refusals reach here too, because it is the shared code
ON ERROR SKIP 2
ONEWIRE WRITE 26, 0, 3, 1, 2
PRINT "short list:      ";MM.ERRMSG$
ON ERROR SKIP 2
ONEWIRE READ 26, 0, 40, b()
PRINT "small array:     ";MM.ERRMSG$

' TEMPR's two forms
TEMPR START 26
TEMPR START 26, 3
TEMPR START 26, 3, 900
PRINT "TEMPR START translated"
PRINT "done"

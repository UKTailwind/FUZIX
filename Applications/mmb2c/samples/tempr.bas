' ONEWIRE and TEMPR - the DS18B20 on GP26.
'
' The data arguments are the ones I2C and SPI take, because MMBasic's
' owWrite and owRead call the same GetCommsTxData / GetCommsRxDest that
' I2C.c and SPI.c do.  So the forms below should look familiar.
'
' The flag is MMBasic's:  1 reset first, 2 reset after,
'                         4 single bits, 8 strong pull-up
Const OWPIN = 26
Dim integer id(8), i, t
Dim float degc
Dim s$

Print "ONEWIRE on GP"; Str$(OWPIN, 0, 0)

' --- is anything there?  ONEWIRE RESET is a statement and the answer
' --- lands in MM.ONEWIRE, which is how MMBasic spells it too.
OneWire Reset OWPIN
Print "reset: ";
If MM.OneWire Then Print "a device answered" Else Print "NOTHING ANSWERED"

' --- read the ROM, into an array: 8 bytes, family code first ---
OneWire Write OWPIN, 1, 1, &H33          ' reset, then READ ROM
OneWire Read OWPIN, 0, 8, id()
Print "ROM:   ";
For i = 0 To 7
  Print Hex$(id(i), 2); " ";
Next i
Print
If id(0) = &H28 Then
  Print "       family &H28 - a DS18B20"
Else
  Print "       family "; Hex$(id(0), 2); " - not a DS18B20"
EndIf

' --- the same read into a string, which must agree ---
OneWire Write OWPIN, 1, 1, &H33
OneWire Read OWPIN, 0, 8, s$
Print "same as a string: ";
If Asc(s$) = id(0) And Len(s$) = 8 Then Print "ok" Else Print "DIFFERS"

' --- TEMPR the short way: it starts a conversion and waits ---
t = Timer
degc = Tempr(OWPIN)
t = Timer - t
Print "TEMPR(pin):        "; Str$(degc, 0, 2); " C  in "; Str$(t, 0, 0); " ms"

' --- and the two-part way, which is what it is for ---
' TEMPR START begins the conversion and returns AT ONCE, so the program
' does something useful while the sensor works.
t = Timer
Tempr Start OWPIN, 3                    ' 12 bits: 750 ms of conversion
t = Timer - t
Print "TEMPR START:       returned in "; Str$(t, 0, 0); " ms"
t = Timer
degc = Tempr(OWPIN)
t = Timer - t
Print "then TEMPR(pin):   "; Str$(degc, 0, 2); " C  in "; Str$(t, 0, 0); " ms"

' The wait SLEEPS rather than spinning, which MMBasic cannot do because
' it has no one else to run.  Proof: a SETTICK handler keeps firing
' through a 750 ms conversion.
Dim integer ticks
SetTick 100, tock, 1
Tempr Start OWPIN, 3
ticks = 0
degc = Tempr(OWPIN)
SetTick 0, tock, 1
Print "ticks during a 12-bit conversion: "; Str$(ticks, 0, 0);
If ticks >= 5 Then
  Print "   ok - slept AND serviced"
Else
  Print "   the wait did not service the tick"
EndIf
Print "done"
End

Sub tock
  ticks = ticks + 1
End Sub

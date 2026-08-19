' Scan the fixed I2C bus - GP20/GP21, the QWIIC socket.
'
' This is what I2C CHECK exists for, and it is also the honest way to
' tell a wiring fault from a software one: the DS3231 real time clock
' is on this same bus at &h68 and is always fitted, so a scan that
' finds the clock has proved the bus works whatever else is missing.

Option Explicit
Dim a%, n%

Print "Scanning the QWIIC bus (GP20/GP21)..."
Print
For a% = 8 To 119
  I2C CHECK a%
  If MM.I2C = 0 Then
    Print "  &h" + Hex$(a%, 2);
    If a% = &h68 Then Print "  DS3231 real time clock (always fitted)";
    If a% = &h77 Then Print "  BMP180 / BMP085";
    If a% = &h76 Then Print "  BMP280 / BME280";
    If a% = &h57 Then Print "  24C32 EEPROM (on many DS3231 boards)";
    Print
    Inc n%
  EndIf
Next a%
Print
Print n%; " device(s) answered"
If n% = 0 Then
  Print "Nothing at all - if even the clock is silent the bus itself"
  Print "is not working, not the device you plugged in."
EndIf

' i2c0.bas - the FIXED I2C bus: I2C READ, I2C WRITE and I2C CHECK.
'
' GP20/GP21, the QWIIC socket and the DS3231 together.  No SETPIN, no
' OPEN and no CLOSE: the pins are the board's and the controller is
' already running for the clock.
'
' NOTHING HERE RAISES.  MMBasic's I2C transfers record the outcome in
' MM.I2C - 0 it worked, 1 nothing answered, 2 it started and stopped -
' and return; the program is expected to look.  That is what makes
' I2C CHECK useful and what a bus scan needs.
'
' Off the board there is no controller, so every transfer reports 1.
' The point of running this under the gates is that the SHAPES all
' translate, run and leave their destinations in a defined state.

Option Explicit

Dim d%(7), i%, n%
Dim s$, g$

Print "-- CHECK an address with nothing on it"
I2C CHECK &h76
Print "mm.i2c   "; MM.I2C

Print "-- a write, and the status it leaves"
I2C WRITE &h77, 0, 2, &hF4, &h2E
Print "mm.i2c   "; MM.I2C

Print "-- a read into an array leaves it DEFINED, not stale"
For i% = 0 To 7 : d%(i%) = 99 : Next i%
I2C READ &h77, 0, 4, d%()
Print "mm.i2c   "; MM.I2C
Print "bytes    ";
For i% = 0 To 3 : Print d%(i%); : Next i%
Print
Print "untouched"; d%(4)

Print "-- and into a string, which is how STR2BIN gets its fields"
s$ = "xxxxxxxx"
I2C READ &h77, 0, 4, s$
Print "mm.i2c   "; MM.I2C
Print "len      "; Len(s$)

Print "-- the hold option, for a combined write-then-read"
I2C WRITE &h77, 1, 1, &hAA
Print "mm.i2c   "; MM.I2C
I2C READ &h77, 0, 2, d%()
Print "mm.i2c   "; MM.I2C

Print "-- a scan is the shape CHECK exists for"
n% = 0
For i% = 8 To 119
  I2C CHECK i%
  If MM.I2C = 0 Then Inc n%
Next i%
Print "found    "; n%

' I2C2 BEHAVES THE SAME WAY, and that is a change: it used to raise on
' a failed transfer, which was louder than MMBasic rather than quieter
' and still wrong - a program written against the interpreter carries
' on past a device that did not answer.  Reaching the line after these
' is the whole test.
Print "-- I2C2 does not raise either"
SetPin 38, 39, I2C2
On Error Skip 1
I2C2 OPEN 400, 1000
I2C2 WRITE &h77, 0, 1, &hAA
Print "after write"; MM.I2C
I2C2 READ &h77, 0, 2, d%()
Print "after read "; MM.I2C
I2C2 CLOSE

Print "done"

' PORT - eight pins as one number
'
' GP0-GP7 are the first eight pins on the I/O header, so this is an
' eight-bit output port with nothing else needed.  Put LEDs on them if
' you want to watch it; the checks below prove it either way, because
' an output pin reads back what it is being driven to.
'
' The bit order is the one thing worth remembering: THE FIRST PIN IS
' THE LEAST SIGNIFICANT BIT.  PORT(0,8) = 1 lights GP0, not GP7.
'
' Why PORT rather than eight PIN() writes: every pin changes on the
' same clock edge.  Eight separate writes put seven wrong values on the
' bus first, and anything clocked off it - a latch, a display, another
' processor - sees them.

Dim integer i, v, bad

For i = 0 To 7
  SetPin i, DOut
Next i

Print "PORT self-check"
Print

' --- the bit mapping ---------------------------------------------------
Port(0, 8) = &b10110001
Print "PORT(0,8) = &b10110001"
Print "  reads back  "; Bin$(Port(0, 8), 8);
If Port(0, 8) = &b10110001 Then Print "  ok" Else Print "  FAILED"
Print "  GP0="; Pin(0); " GP4="; Pin(4); " GP7="; Pin(7);
Print "   (bit0 on GP0, bit7 on GP7)"
Print

' --- every value round trips -------------------------------------------
bad = 0
For v = 0 To 255
  Port(0, 8) = v
  If Port(0, 8) <> v Then bad = bad + 1
Next v
Print "all 256 values written and read: ";
If bad = 0 Then Print "ok" Else Print bad; " wrong"
Print

' --- two groups --------------------------------------------------------
' The FIRST group takes the low bits.  Split as two nibbles it must give
' the same answer as one group of eight.
bad = 0
For v = 0 To 255
  Port(0, 4, 4, 4) = v
  If Port(0, 8) <> v Then bad = bad + 1
Next v
Print "two groups agree with one: ";
If bad = 0 Then Print "ok" Else Print bad; " wrong"
Print

' --- a counter to watch ------------------------------------------------
Print "counting on GP0-GP7..."
For v = 0 To 255
  Port(0, 8) = v
  Pause 20
Next v
Port(0, 8) = 0
Print "done"

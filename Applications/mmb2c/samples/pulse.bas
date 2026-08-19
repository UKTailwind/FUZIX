' PULSE - a timed inversion on one pin
'
' PULSE INVERTS the pin.  It does not drive it high: it flips whatever
' the pin is, waits, and flips back.  A pulse on a pin sitting low is a
' high-going pulse; on one sitting high it is a low-going one.
'
' Under 3 ms the statement blocks and the width is exact - that is the
' case for a trigger pulse, and it is what you want.  At 3 ms and above
' it returns AT ONCE and the pin flips back later, so the program can
' get on with something while a light stays on.
'
' The later flip needs the program to come up for air: any PAUSE, any
' later PULSE.  A program that starts a long pulse and then computes
' without pausing holds the pin until it next pauses.  MMBasic ends it
' from a hardware timer; Fuzix has no sub-second timer to hang one on.

Dim integer t

SetPin 0, DOut
Pin(0) = 0

Print "PULSE self-check"
Print

' --- the short form blocks --------------------------------------------
t = Timer
Pulse 0, 2
Print "PULSE 0,2 (short form)"
Print "  took "; Str$(Timer - t, 0, 1); " ms - it waited"
Print "  pin is back to "; Pin(0);
If Pin(0) = 0 Then Print "  ok" Else Print "  FAILED"
Print

' --- the long form does not -------------------------------------------
t = Timer
Pulse 0, 200
Print "PULSE 0,200 (long form)"
Print "  took "; Str$(Timer - t, 0, 1); " ms - it returned at once"
Print "  pin is still "; Pin(0);
If Pin(0) = 1 Then Print "  ok (inverted)" Else Print "  FAILED"
Pause 300
Print "  after PAUSE 300 it is "; Pin(0);
If Pin(0) = 0 Then Print "  ok (flipped back)" Else Print "  FAILED"
Print

' --- it inverts, it does not set --------------------------------------
Pin(0) = 1
Pulse 0, 2
Print "from high, PULSE leaves it "; Pin(0);
If Pin(0) = 1 Then Print "  ok (a low-going pulse)" Else Print "  FAILED"
Pin(0) = 0
Print
Print "done"

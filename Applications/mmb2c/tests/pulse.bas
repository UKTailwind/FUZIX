' PULSE - what can be checked without a real clock.
'
' Deliberately NOT a timing test.  A plain host build measures time with
' clock(), which counts processor time and barely moves across a sleep,
' so anything that waited for a pulse to expire would say one thing here
' and another under fcc - and cgate compares the two.  The timing lives
' in samples/pulse.bas, which runs on the board where the clock is real.
'
' What IS the same everywhere: PULSE inverts rather than sets, the short
' form has finished by the next statement, the long form has not, and a
' width of zero ends a running pulse on the spot.

SetPin 0, DOut
Pin(0) = 0

' short form: blocks, so it is over by the next line
Pulse 0, 1
Print "after short pulse  pin0 = "; Pin(0)

' from high it is a low-going pulse - it inverts, it does not set
Pin(0) = 1
Pulse 0, 1
Print "short from high    pin0 = "; Pin(0)
Pin(0) = 0

' long form: returns at once, so the pin is still inverted here
Pulse 0, 500
Print "during long pulse  pin0 = "; Pin(0)

' width zero on a pin already pulsing ends it now, whatever the clock
Pulse 0, 0
Print "after PULSE 0,0    pin0 = "; Pin(0)

' PWM and SERVO - SETPIN pin, PWM, the PWM statement and SERVO.
'
' There is no hardware under the gates, so nothing comes out of a pin;
' what this proves is that both translators agree, that the arithmetic
' runs without tripping a range check, and that the whole spread of
' frequencies compiles - including the low ones that only fit because
' the clock divider takes up the slack.
SETPIN 2, PWM
SETPIN 3, PWM
PRINT "pins set"

' 1kHz, quarter duty on A, three-quarters on B.  1kHz needs the divider:
' 375MHz over 1kHz is 375000, well past the 16-bit counter.
PWM 1, 1000, 25, 75
PRINT "1k"

' 20kHz fits the counter without dividing at all.  One duty only, which
' leaves channel B exactly where it was.
PWM 1, 20000, 50
PRINT "20k"

' A negative duty asks for an inverted output.
PWM 1, 20000, -50
PRINT "inverted"

' 50Hz - a servo frame, and the slowest thing anyone usually wants.
PWM 1, 50, 7.5
PRINT "50Hz"

PWM 1, OFF
PRINT "off"

' SERVO: the same slice, positions rather than duty cycles.  0 is a 1ms
' pulse, 50 is 1.5ms and 100 is 2ms, all in a 20ms frame.
SERVO 1, 50
PRINT "centre"
SERVO 1, 0, 100
PRINT "both ends"
SERVO 1, -20
PRINT "over-travel low"
SERVO 1, 120
PRINT "over-travel high"
SERVO 1, OFF
PRINT "servo off"
PRINT "done"

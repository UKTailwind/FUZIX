' Pulsin( against the board's own PWM - the bench link GP3 to GP5.
'
' The PWM and the timer the kernel stamps the edges with are both
' crystal-derived, so a correct reading is EXACT rather than close: a
' 1 kHz wave at 25% is 250 us high and 750 us low, and that is what
' should come back, whatever else the machine is doing at the time.
'
' Run it once alone, and once with something eating the CPU:
'
'     ./spingap 9 & ./pulsintest
'
' A busy-wait measurement collapses under the second - this machine's
' timeslice is half a second - and a timestamped one does not move.
' PLAN-pulsin.md is the design and utils/spingap.c is the measurement.
Option Explicit

Dim integer k

SetPin GP3, PWM
SetPin GP5, DIN

' slice 1 is GP2 (channel A) and GP3 (channel B); only B is wired
PWM 1, 1000, 0, 25

Print "high pulses, want 250"
For k = 1 To 5
  Print Pulsin(GP5, 1)
Next k

Print "low pulses, want 750"
For k = 1 To 3
  Print Pulsin(GP5, 0)
Next k

' 100 Hz at 10% is a 1 ms pulse - long enough to cross a system tick,
' which is where a busy-wait would start losing microseconds
PWM 1, 100, 0, 10
Print "high pulses, want 1000"
For k = 1 To 3
  Print Pulsin(GP5, 1)
Next k

' and a silent pin is -1, as it is in MMBasic
PWM 1, OFF
Print "silent pin, want -1"
Print Pulsin(GP5, 1, 20000)
Print Pulsin(GP5, 0, 20000)

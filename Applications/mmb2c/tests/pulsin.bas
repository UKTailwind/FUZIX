' Pulsin( and Distance( - the answers that do not need a signal.
'
' A host build has no pins, so every measurement here times out, which
' is exactly what a real board returns for a silent pin: -1 from
' Pulsin(, and -2 from Distance( for "no acknowledgement".  What this
' gate actually proves is the argument handling, the refusals, and that
' the timeout paths terminate at all.
Option Explicit

SetPin GP4, DIN

' the two forms of the timeout: default 100ms, then explicit
Print Pulsin(GP4, 1, 5000)
Print Pulsin(GP4, 0, 5000, 8000)

' a 3-pin device (one argument) and a 4-pin one
Print Distance(GP4)
Print Distance(GP5, GP4)

' the pins this machine can capture on are GP4-GP7, and anything else
' is refused BY NAME rather than measured badly
On Error Skip 1
Print Pulsin(GP8, 1, 5000)
Print MM.ErrMsg$

' the pin must be an input first, as it must be in MMBasic
SetPin GP5, DOUT
On Error Skip 1
Print Pulsin(GP5, 1, 5000)
Print MM.ErrMsg$

' and the reference's own range on the timeouts: 5 us to 10 seconds
On Error Skip 1
Print Pulsin(GP4, 1, 4)
Print MM.ErrMsg$
On Error Skip 1
Print Pulsin(GP4, 1, 10000001)
Print MM.ErrMsg$

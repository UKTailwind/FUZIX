' The counting inputs: SETPIN FIN / CIN / PER (PLAN-count.md).
'
' Under the gates nothing counts: the host models the kernel's
' counters as cells that configure-to-zero, store, and read back.  So
' what this file proves is the compile shape of all three modes with
' and without their optional argument, the mode plumbing through
' SETPIN/OFF/reconfigure, Pin()=v on a counting input, and the scaling
' arithmetic PIN() wraps around the raw counter - the board acceptance
' (utils/cnttest.c and the stage-3 list) proves the counting itself.
SetPin gp4, FIN
SetPin gp5, CIN
SetPin gp6, PIN, 4
SetPin gp7, CIN, 3
Print Pin(gp4)
Print Pin(gp5)
Pin(gp5) = 7
Print Pin(gp5)
Pin(gp5) = 0
Print Pin(gp5)
' the gate is remembered per pin for the Hz scaling
SetPin gp4, FIN, 250
Print Pin(gp4)
' period reads scale by the cycles averaged
Print Pin(gp6)
' a counting pin returns to ordinary duty
SetPin gp5, OFF
SetPin gp5, DIN
Print Pin(gp5)
' the optional argument is an expression, like the pin
Dim g As Integer = 100
SetPin gp7, FIN, g * 10
Print Pin(gp7)

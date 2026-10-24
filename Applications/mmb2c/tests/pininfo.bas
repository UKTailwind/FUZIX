' MM.INFO(PIN n): the pin's function as the firmware names it.  A pin
' this program set is named, a PWM one by slice and channel; GP1 on the
' I/O header is OFF until then; GP12 is the board's.  Picofrog asks this
' of GP1-GP3 before it looks for a NES pad on them.
Print Mm.Info(pin 1)
SetPin 1, Din, PullUp
Print Mm.Info(pin 1)
SetPin 2, Dout
Print Mm.Info(pin 2)
SetPin 4, PWM
Print Mm.Info(pin 4)
Print Mm.Info(pin 12)
Print Mm.Info(pin 99)

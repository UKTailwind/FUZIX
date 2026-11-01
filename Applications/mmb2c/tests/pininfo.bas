' MM.INFO(PIN n): the pin's function as the firmware names it.  A pin
' this program set is named, a PWM one by slice and channel (GP4 is
' slice 2, channel A); GP1 on the I/O header is OFF until then.  Picofrog
' asks this of GP1-GP3 before it looks for a NES pad on them.  The pins
' are named by GP number: a plain number is the physical pin, so pin 4 is
' GP2 and pin 12 is GP9 (the PC3's console).  A pin the board reserves is
' left out: the firmware names its use (HDMI, CONSOLE RX), which differs
' from board to board.
Print Mm.Info(pin GP1)
SetPin GP1, Din, PullUp
Print Mm.Info(pin GP1)
SetPin GP2, Dout
Print Mm.Info(pin GP2)
SetPin GP4, PWM
Print Mm.Info(pin GP4)
Print Mm.Info(pin 99)

' FILL, both modes, checked by reading pixels back and timed.
Option Explicit
Option Default Integer
Dim t0, tsmall, tbig
Dim rin, rout, redge, bin, bout, big1, big2

Mode 2
Colour Rgb(WHITE), Rgb(BLACK)
Cls

' --- replace mode: an outlined circle, filled from the middle ---
Circle 80, 60, 40, , , Rgb(WHITE)
t0 = Timer
Fill 80, 60, Rgb(RED)
tsmall = Timer - t0
rin   = Pixel(80, 60)      ' centre: filled
redge = Pixel(80, 20)      ' on the outline: still white
rout  = Pixel(80, 5)       ' outside: untouched

' --- boundary mode: fill up to a colour, across other colours ---
Cls
Box 10, 10, 140, 100, , Rgb(GREEN), Rgb(BLACK)
Line 75, 10, 75, 109, , Rgb(GREEN)      ' a divider in the boundary colour
Pixel 40, 50, Rgb(BLUE)                 ' some other colour inside
Fill 30, 50, Rgb(YELLOW), Rgb(GREEN)
bin  = Pixel(40, 50)       ' the blue pixel got filled over: boundary mode
bout = Pixel(100, 50)      ' the far side of the divider: untouched

' --- a whole screen, for the timing ---
Cls
t0 = Timer
Fill 160, 120, Rgb(CYAN)
tbig = Timer - t0
big1 = Pixel(5, 5)
big2 = Pixel(310, 230)

Mode 1
Print "replace mode, circle"
Print "  centre  "; Hex$(rin,6);   "  want FF0000"
Print "  outline "; Hex$(redge,6); "  want FFFFFF"
Print "  outside "; Hex$(rout,6);  "  want 000000"
Print "  took "; tsmall; " ms"
Print "boundary mode"
Print "  inside, over blue "; Hex$(bin,6);  "  want FFFF00"
Print "  past the divider  "; Hex$(bout,6); "  want 000000"
Print "whole screen"
Print "  corner "; Hex$(big1,6); " and "; Hex$(big2,6); "  want 00FFFF both"
Print "  took "; tbig; " ms"

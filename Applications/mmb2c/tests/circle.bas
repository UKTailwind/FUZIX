' CIRCLE - the first primitive to live in mmb_gfx.h rather than in
' bcrun.  Every form: default, filled, aspect, explicit colours, and a
' wide border.  Nothing is printed but the count, so this runs under
' the gates on a host with no display.
MODE 2
Timer = 0
Colour RGB(WHITE)
Circle 160, 120, 100
Circle 160, 120, 60, , , RGB(YELLOW)
Circle 160, 120, 30, , , RGB(RED), RGB(BLUE)
Circle 80, 60, 40, , 2.0, RGB(GREEN)
Circle 240, 180, 40, 4, , RGB(CYAN)
For i = 1 To 20
  Circle 160, 120, i * 5, , , RGB(MAGENTA)
Next i
Print "circles drawn"
Print Timer

' BOX and RBOX - every argument form: defaults, line width, colours,
' fill, bare commas, negative width/height, the zero-size guard, and
' the rounded corners at several radii including the degenerate one.
' Nothing is printed but the closing line, so this runs under the
' gates on a host with no display and has an .expected.
MODE 2
Colour RGB(WHITE)
Box 10, 10, 100, 60
Box 10, 80, 100, 60, 3
Box 10, 150, 100, 60, , RGB(YELLOW)
Box 120, 10, 100, 60, 2, RGB(RED), RGB(BLUE)
Box 120, 80, 100, 60, 0, , RGB(GREEN)
Box 229, 209, -100, -60, 1, RGB(CYAN)
Box 120, 150, 100, 0
RBox 230, 10, 80, 60
RBox 230, 80, 80, 60, 20
RBox 230, 150, 80, 60, , RGB(MAGENTA)
RBox 124, 154, 92, 52, 12, RGB(RED), RGB(BLUE)
RBox 240, 160, 60, 40, 0
For i = 1 To 10
  Box 160 - i * 8, 120 - i * 6, i * 16, i * 12
Next i
Print "boxes drawn"

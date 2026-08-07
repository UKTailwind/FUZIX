' TRIANGLE and ARC - outline, fill, the collinear degenerate case,
' bare-comma colours, and arcs with and without the inner radius,
' including a sweep crossing 0 degrees.  Deterministic output only,
' so this runs under the gates on a host with no display.
MODE 2
Colour RGB(WHITE)
Triangle 10, 10, 100, 20, 40, 80
Triangle 120, 10, 210, 20, 150, 80, RGB(YELLOW)
Triangle 10, 100, 100, 110, 40, 170, RGB(RED), RGB(BLUE)
Triangle 120, 100, 210, 110, 150, 170, , RGB(GREEN)
Triangle 220, 10, 240, 30, 260, 50
Arc 270, 60, 40, , 0, 90
Arc 270, 60, 20, 30, 90, 270, RGB(CYAN)
Arc 270, 170, 30, 40, 315, 45, RGB(MAGENTA)
For i = 1 To 5
  Triangle 160, 200, 160 - i * 12, 239, 160 + i * 12, 239
Next i
Print "shapes drawn"

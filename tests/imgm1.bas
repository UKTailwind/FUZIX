' The same round trip in MODE 1 - 640x480, one bit, and the mode the
' console lives in.  Nothing may be printed between the two saves: in
' this mode stdout is rendered INTO the framebuffer being captured, so
' the output goes to a file instead.
MODE 1
Circle 320, 240, 150, , , RGB(WHITE)
Circle 320, 240, 80, , , RGB(WHITE), RGB(WHITE)
Circle 150, 120, 60, 5, , RGB(WHITE)
SAVE IMAGE "m1.bmp"
MODE 1
LOAD IMAGE "m1.bmp"
SAVE IMAGE "m1b.bmp"
SYSTEM "sum", "m1.bmp", "m1b.bmp"
Print "mode 1 round trip done"

' SAVE IMAGE / LOAD IMAGE round trip.
'
' Draw, save, wipe the screen by re-entering the mode, load it back and
' save again.  The source is already a 16 colour picture, so dithering
' has nothing to approximate and the two files must be identical - sum
' says so without anyone having to look at the screen.
MODE 2
Colour RGB(WHITE)
Circle 160, 120, 100, , , RGB(YELLOW), RGB(BLUE)
Circle 80, 60, 30, , , RGB(RED), RGB(GREEN)
Circle 240, 180, 40, 6, , RGB(CYAN)
SAVE IMAGE "a.bmp"
MODE 2
LOAD IMAGE "a.bmp"
SAVE IMAGE "b.bmp"
MODE 1
SYSTEM "sum", "a.bmp", "b.bmp"
Print "round trip done"

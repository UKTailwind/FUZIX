' SAVE IMAGE and SYSTEM.  Draws something, saves the whole screen and
' a crop of it, then runs ls to prove both files arrived - which also
' exercises passing several arguments to a program.
MODE 2
Colour RGB(WHITE)
Circle 160, 120, 100, , , RGB(YELLOW), RGB(BLUE)
Circle 80, 60, 30, , , RGB(RED), RGB(GREEN)
SAVE IMAGE "shot2.bmp"
SAVE IMAGE "crop2.bmp", 60, 40, 64, 32
MODE 1
SYSTEM "ls", "-l", "shot2.bmp", "crop2.bmp"
Print "saved"

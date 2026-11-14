' SAVE PNG: the screen and a part of it as PNGs, then the screen
' reloaded from its PNG.  The picture is saved as a BMP before and
' after, and the two must be the same file (the images gate's round
' trip); ls proves both PNGs arrived, with their sizes.
MODE 2
CLS RGB(BLUE)
Circle 160, 120, 100, , , RGB(YELLOW), RGB(RED)
Box 10, 10, 60, 40, 3, RGB(GREEN), RGB(MAGENTA)
Text 160, 200, "PNG", "CT", 2, 1, RGB(WHITE)
SAVE IMAGE "before.bmp"
SAVE PNG "shotp"
SAVE PNG "cropp.png", 60, 40, 64, 32
CLS
LOAD PNG "shotp.png"
SAVE IMAGE "after.bmp"
MODE 1
SYSTEM "ls", "-l", "shotp.png", "cropp.png"
Print "saved"

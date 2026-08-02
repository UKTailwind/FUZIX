' MODE 1 round trip, on a 320x240 crop rather than the whole 640x480
' screen.  Two full-screen captures in MODE 1 are 921,654 bytes each,
' and writing a pair of those is what immediately preceded a corrupt
' superblock - the damage fsck found was in doubly-indirect blocks,
' which is what a file that size needs.  A crop is 230,454 bytes, the
' size that has worked all along.
'
' The screen is wiped by drawing over it, not by re-entering the mode,
' so the console does not repaint its text into the picture.
MODE 1
For y = 0 To 479
  Line 0, y, 639, y, , 0
Next y
Circle 320, 240, 150, , , RGB(WHITE)
Circle 320, 240, 80, , , RGB(WHITE), RGB(WHITE)
Circle 240, 200, 40, 5, , RGB(WHITE)
SAVE IMAGE "q1.bmp", 160, 120, 320, 240
For y = 0 To 479
  Line 0, y, 639, y, , 0
Next y
LOAD IMAGE "q1.bmp", 160, 120
SAVE IMAGE "q1b.bmp", 160, 120, 320, 240
SYSTEM "sum", "q1.bmp", "q1b.bmp"
Print "mode 1 crop round trip done"

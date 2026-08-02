' The full save / load / save sequence, five times over.
'
' Twenty forks with real file writing between them, which is the shape
' that corrupted the filesystem twice before bcrun learned to sync
' either side of a fork.  Each round must also print the same checksum
' twice: the round trip stays exact, or the loader has drifted.
MODE 1
For n = 1 To 5
  For y = 0 To 479
    Line 0, y, 639, y, , 0
  Next y
  Circle 320, 240, 150, , , RGB(WHITE)
  Circle 320, 240, 80, , , RGB(WHITE), RGB(WHITE)
  Circle 240, 200, 40, 5, , RGB(WHITE)
  SAVE IMAGE "s1.bmp", 160, 120, 320, 240
  For y = 0 To 479
    Line 0, y, 639, y, , 0
  Next y
  LOAD IMAGE "s1.bmp", 160, 120
  SAVE IMAGE "s2.bmp", 160, 120, 320, 240
  SYSTEM "sum", "s1.bmp", "s2.bmp"
Next n
Print "five rounds done"

' Sixteen colour blocks, for comparing the PC3 against MMBasic.
'
' Top row: the sixteen colours a 4-bit mode can show, generated as
' R 1 bit, G 2 bits, B 1 bit - the RGB121 cube MMBasic uses in MODE 2.
' Bottom row: a sweep of arbitrary RGB values, which is what shows how
' the two map colours that are NOT already in the palette.
MODE 2
For i = 0 To 15
  r = (i \ 8) * 255
  g = ((i \ 2) Mod 4) * 64
  b = (i Mod 2) * 255
  c = RGB(r, g, b)
  For x = 0 To 17
    Line i * 20 + x, 20, i * 20 + x, 100, , c
  Next x
Next i
For i = 0 To 31
  c = RGB(i * 8, 255 - i * 8, (i * 33) Mod 256)
  For x = 0 To 8
    Line i * 10 + x, 130, i * 10 + x, 210, , c
  Next x
Next i

' The BMP formats loadimage has to cope with.
'
' g4.bmp and g8r.bmp hold the SAME picture, one as plain 4-bit and one
' as RLE8, so whatever they render to must be identical - which checks
' the RLE decoder against the straightforward one without anybody
' looking at the screen.  g24.bmp is the same picture as a 24-bit
' gradient, which can only be shown by dithering, and it is left up.
MODE 2
LOAD IMAGE "g4.bmp"
SAVE IMAGE "p4.bmp", 0, 0, 96, 72
MODE 2
LOAD IMAGE "g8r.bmp"
SAVE IMAGE "p8.bmp", 0, 0, 96, 72
MODE 2
LOAD IMAGE "g24.bmp"
SYSTEM "sum", "p4.bmp", "p8.bmp"
Print "formats done"

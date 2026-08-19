' Is the blit cost PER CROSSING or PER PIXEL?
'
' 2000 characters cost 55ms - 28us each, and a character is one whole
' kernel crossing that draws a glyph - so a crossing is cheap.  Yet a
' 576-pixel tile costs 830us.  Settle it by holding the pixels constant
' and changing only the number of blits: 77 small tiles against a few
' big ones covering the same area.  If the big ones are much quicker
' the cost is per crossing; if they cost the same it is per pixel.

Mode 2
FrameBuffer Create
FrameBuffer Layer 9
Open "/dev/tty" For output As #2

Dim integer s24(40), s96(600), a24, a96, i, t0, t1, x

a24 = Peek(varaddr s24())
Poke short a24, 24
Poke short a24 + 2, 24
For i = 0 To 287 : Poke byte a24 + 4 + i, &h12 : Next

' 96x96 = 9216 pixels = 4608 bytes -> 16 of these = one 24x24 tile x 256
a96 = Peek(varaddr s96())
Poke short a96, 96
Poke short a96 + 2, 96
For i = 0 To 4607 : Poke byte a96 + 4 + i, &h12 : Next

FrameBuffer Write f

' 77 x (24x24) = 44,352 pixels in 77 blits
t0 = Timer
For i = 1 To 77 : Blit memory a24, (i Mod 11) * 24, (i Mod 7) * 24 : Next
t1 = Timer
Print #2, "44352 px, 77 blits "; t1 - t0; " ms"

' 5 x (96x96) = 46,080 pixels in 5 blits - same area, 15x fewer blits
t0 = Timer
For i = 1 To 5 : Blit memory a96, (i Mod 2) * 96, (i Mod 2) * 96 : Next
t1 = Timer
Print #2, "46080 px,  5 blits "; t1 - t0; " ms"

' one crossing, nothing else: PIXEL read
t0 = Timer
For i = 1 To 2000 : x = Pixel(10, 10) : Next
t1 = Timer
Print #2, "2000 PIXEL reads   "; t1 - t0; " ms"

' and a bare loop of the same length, to subtract the interpreter
t0 = Timer
For i = 1 To 2000 : x = i : Next
t1 = Timer
Print #2, "2000 bare loops    "; t1 - t0; " ms"

Close #2
FrameBuffer Close

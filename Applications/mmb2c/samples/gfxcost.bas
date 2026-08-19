' Where does a PETSCII Robots movement step actually go?
'
' One step is: writeworld_n (77 tile BLITs), CLS on the layer,
' writesprites_l (up to 28 SPRITEs) and one full-screen MERGE.  Time
' each on its own, at the counts the game uses, and the one that is
' seconds is the one to fix.  Results go to /dev/tty: in a graphics
' mode PRINT never reaches the console.

Mode 2
FrameBuffer Create
FrameBuffer Layer 9

Open "/dev/tty" For output As #2

Dim integer spr(40), a, i, t0, t1

' a plain 24x24 uncompressed 4bpp sprite, the size of a world tile
a = Peek(varaddr spr())
Poke short a, 24
Poke short a + 2, 24
For i = 0 To 287 : Poke byte a + 4 + i, &h12 : Next

FrameBuffer Write f
t0 = Timer
For i = 1 To 77 : Blit memory a, (i Mod 11) * 24, (i Mod 7) * 24 : Next
t1 = Timer
Print #2, "77 tile blits   "; t1 - t0; " ms"

FrameBuffer Write l
t0 = Timer
For i = 1 To 10 : CLS : Next
t1 = Timer
Print #2, "10 layer CLS    "; t1 - t0; " ms"

t0 = Timer
For i = 1 To 28 : Sprite memory a, (i Mod 11) * 24, (i Mod 7) * 24, 9 : Next
t1 = Timer
Print #2, "28 sprites      "; t1 - t0; " ms"

FrameBuffer Write f
t0 = Timer
For i = 1 To 10 : FrameBuffer Merge 9 : Next
t1 = Timer
Print #2, "10 merges       "; t1 - t0; " ms"

t0 = Timer
For i = 1 To 10 : FrameBuffer Merge 9, b : Next
t1 = Timer
Print #2, "10 merges w/ B  "; t1 - t0; " ms"

t0 = Timer
For i = 1 To 2000 : Text 8, 8, "x",,,, 15, 0 : Next
t1 = Timer
Print #2, "2000 chars      "; t1 - t0; " ms"

Close #2
FrameBuffer Close

' 10,000 filled circles at random positions, sizes and colours, with a
' black outline.  A stress test for the span path in mmb_gfx.h and for
' RND, rather than a picture.
MODE 2
Timer =0
count=0
Do
r = Rnd * 255
g = Rnd * 255
b = Rnd * 255
Circle Rnd * MM.HRES, Rnd * MM.VRES, Rnd * 40,,, 0, RGB(r,g,b)
Inc count
Loop Until count =10000
Print Timer
End

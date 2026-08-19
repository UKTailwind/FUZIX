' TEST: does shrinking MM_BATCH cost anything measurable?
'
' MM_BATCH sizes three static buffers that live in every BASIC process,
' so it is a memory decision - but it is also how many points ride one
' kernel crossing, so it has to be shown NOT to cost speed before the
' memory is taken.  This draws the shapes that go through the batch:
' scattered PIXELs one at a time, a PIXEL array in one call, and the
' diagonal lines that batch rather than becoming rectangles.
'
' Run it with the old runtime and the new one and compare.

Mode 2
Open "/dev/tty" For output As #2

Dim integer i, j, t0, t1
Dim integer px(999), py(999), pc(999)

' 1. scalar PIXEL, the worst case for batching: 40,000 of them
t0 = Timer
For j = 1 To 40
  For i = 0 To 999
    Pixel i Mod 320, (i * 7 + j) Mod 240, i And 15
  Next
Next
t1 = Timer
Print #2, "40000 scalar PIXEL "; t1 - t0; " ms"

' 2. the array form - one call, the runtime does the batching
For i = 0 To 999
  px(i) = i Mod 320
  py(i) = (i * 3) Mod 240
  pc(i) = RGB(white)
Next
t0 = Timer
For j = 1 To 40
  Pixel px(), py(), pc()
Next
t1 = Timer
Print #2, "40 x 1000 array    "; t1 - t0; " ms"

' 3. diagonal lines: Bresenham into the batch, not rectangles
t0 = Timer
For j = 1 To 200
  Line 0, 0, 319, 239
Next
t1 = Timer
Print #2, "200 diagonal lines "; t1 - t0; " ms"

' 4. circles, which are spans plus points
t0 = Timer
For j = 1 To 100
  Circle 160, 120, 100
Next
t1 = Timer
Print #2, "100 circles        "; t1 - t0; " ms"

Close #2

' PIXEL's array form - one call for a whole run of points.
'
' The gates have no display, so what is checked here is the half that
' does not need one: that all four shapes translate, and that a run
' with mismatched array sizes is bounded by the SHORTEST of them
' rather than walking off the end of one.  The pixels themselves are
' checked on the board, where they can be read back.

Option Explicit
Dim Integer xi(9), yi(9), ci(9)
Dim Float xf(9), yf(9), cf(9)
Dim Integer short(4)
Dim Integer i

For i = 0 To 9
  xi(i) = i * 7
  yi(i) = i * 3
  ci(i) = Rgb(255, 0, 0)
  xf(i) = i * 7
  yf(i) = i * 3
  cf(i) = Rgb(0, 255, 0)
Next i
For i = 0 To 4
  short(i) = i
Next i

Print "-- the four shapes --"

' no colour at all: the whole run in whatever COLOUR last set
Colour Rgb(WHITE)
Pixel xi(), yi()
Print "  integer arrays, current colour"

' one colour for the whole run
Pixel xi(), yi(), Rgb(0, 0, 255)
Print "  integer arrays, one colour"

' a colour per point
Pixel xi(), yi(), ci()
Print "  integer arrays, colour array"

' float coordinates, which MMBasic allows and truncates
Pixel xf(), yf(), cf()
Print "  float arrays, float colour array"

' and the mixtures
Pixel xf(), yi(), ci()
Print "  mixed float and integer"

Print "-- mismatched lengths are bounded by the shortest --"
Pixel short(), yi()
Print "  x shorter than y"
Pixel xi(), short()
Print "  y shorter than x"
Pixel xi(), yi(), short()
Print "  colour array shortest"

Print "-- the scalar form still works --"
Pixel 10, 20
Pixel 10, 20, Rgb(255, 255, 0)
Print "  ok"

Print "-- done --"

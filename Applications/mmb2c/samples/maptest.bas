' MAP - watch the screen.  Nothing here is judged by reading pixels
' back, because a remap changes the PALETTE and not one stored pixel:
' the picture is drawn ONCE, at the start, and never touched again.
' Everything after that is the palette moving underneath it.
'
' Sixteen bars, one per colour number.  MAP(i) is the colour a program
' must ask for to land on entry i, which is exactly what is wanted.

Option Explicit
Option Default Float
Dim Integer i, j, x, r, g, b
Dim Integer d(15)

Mode 2
Cls Rgb(BLACK)

' --- draw once: sixteen bars, colour number 0 to 15, left to right ---
For i = 0 To 15
  d(i) = Map(i)
  For x = i * 20 To i * 20 + 18
    Line x, 0, x, 199, , d(i)
  Next x
Next i
Print @(0, 210) "default palette"
Pause 2500

' --- the same picture in greys.  No pixel is redrawn. ---
Map Greyscale
Print @(0, 210) "MAP GREYSCALE  "
Pause 2500

' --- the Colour Maximite's sixteen ---
Map Maximite
Print @(0, 210) "MAP MAXIMITE   "
Pause 2500

' --- a fade to black, by remapping only ---
Map Reset
Print @(0, 210) "fade to black  "
For j = 15 To 0 Step -1
  For i = 0 To 15
    r = ((d(i) >> 16) And 255) * j \ 15
    g = ((d(i) >> 8) And 255) * j \ 15
    b = (d(i) And 255) * j \ 15
    Map(i) = Rgb(r, g, b)
  Next i
  Map Set
  Pause 100
Next j

' --- and a colour cycle: rotate the palette, picture untouched ---
Print @(0, 210) "colour cycling "
For j = 1 To 64
  For i = 0 To 15
    Map(i) = d((i + j) And 15)
  Next i
  Map Set
  Pause 120
Next j

Map Reset
Print @(0, 210) "back to default"
Pause 2000
Mode 1
Print "MAP demo finished."
Print "Did the bars change colour without ever being redrawn?"

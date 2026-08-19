' POLYGON on the real display, checked by reading pixels back.
'
' Everything is COLLECTED and printed after Mode 1: in a graphics mode
' PRINT draws on the HDMI screen, so anything reported before returning
' to the console is invisible down the serial line.
Option Explicit
Option Default Integer
Dim px(5), py(5)
Dim fx(2) As Float, fy(2) As Float
Dim i, ink
Dim hcen, hout, hvtx          ' filled hexagon
Dim n0row, n0in               ' n = 0, outline only
Dim tin, tapex, tout          ' float triangle, filled
Dim carm, cnotch              ' concave

Mode 2
Colour Rgb(WHITE), Rgb(BLACK)

' --- filled hexagon, integer arrays ---
Cls
px(0)=60 : py(0)=20
px(1)=100: py(1)=40
px(2)=100: py(2)=80
px(3)=60 : py(3)=100
px(4)=20 : py(4)=80
px(5)=20 : py(5)=40
Polygon 6, px(), py(), Rgb(WHITE), Rgb(RED)
hcen = Pixel(60,60) : hout = Pixel(5,5) : hvtx = Pixel(60,20)

' --- n = 0 uses the whole array; outline only ---
Cls
Polygon 0, px(), py(), Rgb(GREEN)
ink = 0
For i = 0 To 119
  If Pixel(i,60) <> 0 Then ink = ink + 1
Next i
n0row = ink : n0in = Pixel(60,60)

' --- float arrays, filled triangle ---
Cls
fx(0)=10.0 : fy(0)=10.0
fx(1)=90.0 : fy(1)=10.0
fx(2)=50.0 : fy(2)=70.0
Polygon 3, fx(), fy(), Rgb(CYAN), Rgb(BLUE)
tin = Pixel(50,30) : tapex = Pixel(50,70) : tout = Pixel(12,60)

' --- concave: the fill must not bridge the notch ---
Cls
px(0)=20 : py(0)=20
px(1)=100: py(1)=20
px(2)=100: py(2)=100
px(3)=60 : py(3)=50
px(4)=20 : py(4)=100
px(5)=20 : py(5)=20
Polygon 6, px(), py(), Rgb(WHITE), Rgb(YELLOW)
carm = Pixel(30,90) : cnotch = Pixel(60,95)

Mode 1
Print "filled hexagon"
Print "  centre  "; Hex$(hcen,6); "  want FF0000"
Print "  outside "; Hex$(hout,6); "  want 000000"
Print "  vertex  "; Hex$(hvtx,6); "  want FFFFFF"
Print "n=0 (whole array), outline only"
Print "  lit on row 60 "; n0row; "  want 2 (the two sides)"
Print "  interior "; Hex$(n0in,6); "  want 000000"
Print "float arrays, filled triangle"
Print "  inside  "; Hex$(tin,6); "  want 0000FF"
Print "  apex    "; Hex$(tapex,6); "  want 00FFFF"
Print "  outside "; Hex$(tout,6); "  want 000000"
Print "concave arrowhead"
Print "  in the left arm "; Hex$(carm,6); "  want FFFF00"
Print "  in the notch    "; Hex$(cnotch,6); "  want 000000"

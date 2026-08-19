' BEZIER, checked by reading pixels back.  Collected and printed after
' Mode 1: in a graphics mode PRINT draws on the screen and scrolls it.
Option Explicit
Option Default Integer
Dim bx(3), by(3)
Dim qx(2), qy(2)
Dim i, ink
Dim startpx, endpx, midlit, above, below
Dim qstart, qend, qmid

Mode 2
Colour Rgb(WHITE), Rgb(BLACK)
Cls

' --- a cubic S-curve: endpoints must be hit exactly ---
bx(0)=20  : by(0)=100
bx(1)=20  : by(1)=20
bx(2)=140 : by(2)=100
bx(3)=140 : by(3)=20
Bezier bx(), by(), , Rgb(WHITE)
startpx = Pixel(20,100)
endpx   = Pixel(140,20)
' the curve must pass somewhere down the middle column
midlit = 0
For i = 0 To 119
  If Pixel(80,i) <> 0 Then midlit = midlit + 1
Next i
' a cubic with these controls stays inside the box; corners stay clear
above = Pixel(20,20)
below = Pixel(140,100)

' --- a quadratic, three points, in a different colour ---
Cls
qx(0)=10  : qy(0)=110
qx(1)=80  : qy(1)=10
qx(2)=150 : qy(2)=110
Bezier qx(), qy(), 3, Rgb(CYAN)
qstart = Pixel(10,110)
qend   = Pixel(150,110)
' apex of a quadratic is halfway between the control and the chord:
' y = (110 + 2*10 + 110)/4 = 60, at x = 80
qmid = 0
For i = 55 To 65
  If Pixel(80,i) <> 0 Then qmid = qmid + 1
Next i

Mode 1
Print "cubic S-curve"
Print "  start point "; Hex$(startpx,6); "  want FFFFFF"
Print "  end point   "; Hex$(endpx,6);   "  want FFFFFF"
Print "  column 80 lit "; midlit; "  want > 0"
Print "  corner 20,20 "; Hex$(above,6);  "  want 000000"
Print "  corner 140,100 "; Hex$(below,6);"  want 000000"
Print "quadratic (n=3)"
Print "  start "; Hex$(qstart,6); "  want 00FFFF"
Print "  end   "; Hex$(qend,6);   "  want 00FFFF"
Print "  apex near y=60: "; qmid; " lit of 11 rows  want > 0"

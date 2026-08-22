' WS2812 and BITSTREAM: compile shape and the packing arithmetic under
' the host model - claims succeed, the words are built in the model
' buffer, nothing is driven, the waits return at once.  The board
' acceptance (utils/pioouttest.c and striptest) proves the wire.
Dim Integer c(11)
Dim Integer i
For i = 0 To 11 : c(i) = RGB(255, 0, 0) : Next i
WS2812 B, gp7, 12, c()
WS2812 O, gp7, 12, c()
WS2812 S, gp7, 12, c()
WS2812 W, gp7, 12, c()
WS2812 B, gp7, 1, RGB(0, 255, 0)
Dim Integer d(9)
For i = 0 To 9 : d(i) = 250 : Next i
BITSTREAM gp2, 10, d()
BITSTREAM gp2, 10, d(), 1
Dim Float e(9)
For i = 0 To 9 : e(i) = 100.5 : Next i
BITSTREAM gp2, 10, e()
Print "pioout done"

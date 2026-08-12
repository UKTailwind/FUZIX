' PORT round trip: eight pins driven and read back as one number
Dim integer i
For i = 0 To 7
  SetPin i, DOut
Next i

Port(0, 8) = &b10110001
Print "wrote  10110001 read "; Bin$(Port(0, 8), 8)

Print "pin0 "; Pin(0); " pin1 "; Pin(1); " pin4 "; Pin(4); " pin7 "; Pin(7)

Port(0, 4, 4, 4) = &b11000011
Print "wrote  11000011 read "; Bin$(Port(0, 4, 4, 4), 8); " (two groups)"
Print "                 as one group "; Bin$(Port(0, 8), 8)

Port(0, 8) = 0
Print "cleared          read "; Bin$(Port(0, 8), 8)

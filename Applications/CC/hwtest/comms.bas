' The shared data forms, against the real sensor on I2C2.
'
' The gates can only prove these parse and compile - there is no bus
' under them.  This one talks to the pressure sensor on GP38/39 and
' reads its chip-ID register four different ways.  All four must give
' the same byte, because after the shared layer they are the same code
' with four different destinations.
Dim integer b(8), id, i
Dim float g(8)
Dim s$, v1, v2, v3

SetPin 38, 39, I2C2
I2C2 Open 400, 1000

Print "chip ID register (&HD0), read four ways"

' 1: into a single scalar - the form that did not exist before
I2C2 Write &H77, 1, 1, &HD0
I2C2 Read &H77, 0, 1, v1
Print "  single scalar   "; Hex$(v1, 2)

' 2: into an integer array
I2C2 Write &H77, 1, 1, &HD0
I2C2 Read &H77, 0, 1, b()
Print "  integer array   "; Hex$(b(0), 2)

' 3: into a float array
I2C2 Write &H77, 1, 1, &HD0
I2C2 Read &H77, 0, 1, g()
Print "  float array     "; Hex$(g(0), 2)

' 4: into a string
I2C2 Write &H77, 1, 1, &HD0
I2C2 Read &H77, 0, 1, s$
Print "  string          "; Hex$(Asc(s$), 2)
id = v1

' --- a longer read, into a list of lvalues ---
' The calibration block starts at &HAA.  Three bytes into three
' variables: MMBasic's COMMS_RXD_LIST, which we never had.
I2C2 Write &H77, 1, 1, &HAA
I2C2 Read &H77, 0, 3, v1, v2, v3
Print "cal &HAA..&HAC:  "; Hex$(v1, 2); " "; Hex$(v2, 2); " "; Hex$(v3, 2)

' the same three bytes into an array, which must agree
I2C2 Write &H77, 1, 1, &HAA
I2C2 Read &H77, 0, 3, b()
Print "  as an array:   "; Hex$(b(0), 2); " "; Hex$(b(1), 2); " "; Hex$(b(2), 2)
If b(0) = v1 And b(1) = v2 And b(2) = v3 Then
  Print "  list and array agree   ok"
Else
  Print "  list and array DIFFER  FAILED"
EndIf

' --- writing from a string, which takes no buffer at all ---
s$ = Chr$(&HAA)
I2C2 Write &H77, 1, 1, s$
I2C2 Read &H77, 0, 1, v1
Print "register set from a string: "; Hex$(v1, 2);
If v1 = b(0) Then Print "   ok" Else Print "   FAILED"

' --- the two refusals, on real hardware ---
On Error Skip 2
I2C2 Write &H77, 0, 3, 1, 2
Print "short list:      "; MM.ErrMsg$
On Error Skip 2
I2C2 Read &H77, 0, 40, b()
Print "small array:     "; MM.ErrMsg$

I2C2 Close
Print "done"

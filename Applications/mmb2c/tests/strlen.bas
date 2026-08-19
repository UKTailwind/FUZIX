' DIM s$(n) LENGTH m sets the SPACING of the elements, not just a cap.
' findvar returns val.s + nbr * (size + 1) (MMBasic.c:4924), so a program
' is entitled to walk the array itself at that stride - which is how
' PETSCII Robots holds its world map.

Option default integer

Dim a$(3) Length 8
Dim i, base, seen

a$(0) = "ABCD"
a$(1) = "EFGH"
a$(2) = "IJ"
a$(3) = "12345678"      ' exactly full: no room for a trailing NUL

base = Peek(varaddr a$())

' the length byte of each element, one stride apart
Print "stride";
For i = 0 To 3
  Print " "; Peek(byte base + i * 9);
Next
Print

' the first character of each element, past its length byte
Print "first";
For i = 0 To 3
  Print " "; Chr$(Peek(byte base + i * 9 + 1));
Next
Print

' one element's address is the same arithmetic
Print "elem2 "; Peek(varaddr a$(2)) - base

' a full element still reads back as a proper string
Print "full ["; a$(3); "] len"; Len(a$(3))
Print "cat ["; a$(2) + a$(1); "]"

' and writing a shorter one over it does not disturb its neighbours
a$(3) = "xy"
Print "after";
For i = 0 To 3
  Print " "; Peek(byte base + i * 9);
Next
Print
Print "n1 ["; a$(1); "] n2 ["; a$(2); "]"

' LENGTH on a scalar is still just a cap - the layout cannot differ
Dim s$ Length 4
s$ = "pq"
Print "scalar"; Peek(byte Peek(varaddr s$)); " "; Chr$(Peek(byte Peek(varaddr s$) + 1))

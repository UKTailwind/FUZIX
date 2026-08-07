' TYPE and structures - the slice the translator carries, and the
' layout constants that prove the firmware's byte layout is being
' reproduced (TYPE-SPEC.md).  Deterministic output only.
Option Explicit
Option Default None

Type Point
  x As INTEGER
  y As INTEGER
End Type

' the manual's worked example: 21-byte string padded to 24, then four
' integers - 56 bytes, and STRUCT(SIZEOF) must say so
Type Seg
  name As STRING LENGTH 20
  startX As INTEGER
  startY As INTEGER
  endX As INTEGER
  endY As INTEGER
End Type

' all strings: alignment 1, no tail padding - 21 + 11 = 32... no:
' LENGTH 20 is 21 bytes and LENGTH 10 is 11, so 32 exactly by luck;
' LENGTH 9 gives 21 + 10 = 31, which only an unpadded layout reports
Type Tags
  a As STRING LENGTH 20
  b As STRING LENGTH 9
End Type

' nested, with member arrays
Type Inner
  values(3) As FLOAT
End Type

Type Outer
  id As INTEGER
  items(2) As Inner
  label As STRING LENGTH 10
End Type

Print "sizes: "; Struct(SIZEOF "Point"); " "; Struct(SIZEOF "Seg");
Print " "; Struct(SIZEOF "Tags"); " "; Struct(SIZEOF "Outer")
Print "offsets: "; Struct(OFFSET "Seg", "startX");
Print " "; Struct(OFFSET "Outer", "label")
Print "types: "; Struct(TYPE "Seg", "name"); Struct(TYPE "Point", "x");
Print Struct(TYPE "Inner", "values"); Struct(TYPE "Outer", "items")

Dim p As Point
Dim q As Point
p.x = 100 : p.y = 200
q = p
q.y = 300
Print "copy: "; p.x; p.y; q.x; q.y

Dim s As Seg = ("diag", 1, 2, 3, 4)
Print "init: "; s.name; s.startX; s.startY; s.endX; s.endY

' bounded member strings: LENGTH 9 truncates
Dim t As Tags
t.a = "alpha"
t.b = "0123456789overflow"
Print "bound: "; t.a; " "; t.b; Len(t.b)

' arrays of structs, member arrays, chains
Dim grid(3) As Outer
Dim i As INTEGER
Dim j As INTEGER
For i = 0 To 3
  grid(i).id = i * 10
  grid(i).label = "G" + Str$(i)
  For j = 0 To 2
    grid(i).items(j).values(0) = i + j
    grid(i).items(j).values(3) = i * j
  Next j
Next i
Print "chain: "; grid(2).items(1).values(0); grid(3).items(2).values(3)
Print "label: "; grid(3).label

' by-reference parameters, and LOCAL structs surviving a second call
Sub Bump(pt As Point, n As INTEGER)
  Local w As Point
  w.x = n
  pt.x = pt.x + w.x
  pt.y = pt.y + 1
End Sub

Bump p, 7
Bump p, 7
Print "byref: "; p.x; p.y

' STRUCT verbs
Dim a As Point
Dim b As Point
a.x = 1 : a.y = 2
b.x = 3 : b.y = 4
Struct Swap a, b
Print "swap: "; a.x; a.y; b.x; b.y
Struct Copy a To b
Print "vcopy: "; b.x; b.y
Struct Clear a
Print "clear: "; a.x; a.y

Dim pts(2) As Point
Dim pts2(2) As Point
pts(0).x = 5 : pts(1).x = 6 : pts(2).x = 7
Struct Copy pts() To pts2()
Struct Clear pts()
Print "acopy: "; pts2(0).x; pts2(1).x; pts2(2).x; pts(0).x

' a dotted PLAIN variable keeps working when no struct shares the head
Dim legacy.total As FLOAT
legacy.total = 2.5
Print "dotted: "; legacy.total

Print "done"

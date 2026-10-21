' STRUCT COPY, CLEAR and SWAP on an ELEMENT of a structure array take
' the element every other access takes: under OPTION BASE 1 a(1) is the
' first, and in a 2-D array the first subscript is the adjacent one.
' This path used to index the C array raw - a(1) was the second element
' and a 2-D element was found backwards.  Inside an ON ERROR window its
' subscripts are checked like any other.
Option Base 1
Type pt
  x As Integer
End Type
Dim a(3) As pt, g(2, 3) As pt, t As pt
a(1).x = 11 : a(2).x = 22 : a(3).x = 33
Struct Copy a(1) To t
Print "copy a(1): "; t.x
t.x = 99
Struct Copy t To a(3)
Print "a(3) now "; a(3).x; ", a(2) still "; a(2).x
Struct Swap a(1), a(2)
Print "swapped: "; a(1).x; a(2).x
g(2, 1).x = 21 : g(1, 3).x = 13
Struct Copy g(2, 1) To t
Print "g(2,1): "; t.x
Struct Clear g(1, 3)
Print "g(1,3) cleared: "; g(1, 3).x; ", g(2,1) still "; g(2, 1).x
ON ERROR SKIP 1
Struct Clear a(4)
Print "a(4): " MM.ERRMSG$
ON ERROR SKIP 1
Struct Copy g(3, 1) To t
Print "g(3,1): " MM.ERRMSG$

' ARRAY SLICE, ARRAY INSERT and COLOUR MAP.
'
' A slice is one line through an array of two or more dimensions: every
' index is given but one, and the line runs along the blank one.  MMBasic
' works the addresses out at run time from its own storage order; here the
' translator knows the shape, so it hands the runtime a start, a stride
' and a count.  The storage orders are opposites - MMBasic's first
' subscript is adjacent, a C array's last is - so these tests exist to
' prove the SET OF ELEMENTS is the same even though the addresses are not.
'
' MATH SLICE and MATH INSERT are the same two commands: MMBasic's cmd_math
' calls array_slice and array_insert, exactly as cmd_slice and cmd_insert
' do.  Both spellings are checked.
Dim integer a(3, 4, 5)
Dim integer b(4), i, j, k, n
Dim float f(2, 3), g(3)
Dim s$(2, 3), t$(3)

' Fill a() so that every element names itself.
For i = 0 To 3
  For j = 0 To 4
    For k = 0 To 5
      a(i, j, k) = i * 100 + j * 10 + k
    Next k
  Next j
Next i

Print "ARRAY SLICE - the manual's own example, on OPTION BASE 0"
Array Slice a(), 2, , 3, b()
Print "  a(2,*,3) ="; b(0); b(1); b(2); b(3); b(4)

Print "  the blank index last"
Dim integer c(5)
Array Slice a(), 1, 2, , c()
Print "  a(1,2,*) ="; c(0); c(1); c(2); c(3); c(4); c(5)

Print "  the blank index first"
Dim integer d(3)
Array Slice a(), , 4, 5, d()
Print "  a(*,4,5) ="; d(0); d(1); d(2); d(3)

Print
Print "MATH SLICE is the same command"
Array Set 0, b()
Math Slice a(), 2, , 3, b()
Print "  a(2,*,3) ="; b(0); b(1); b(2); b(3); b(4)

Print
Print "ARRAY INSERT puts one back"
For i = 0 To 4
  b(i) = 900 + i
Next i
Array Insert a(), 2, , 3, b()
Print "  a(2,0..4,3) ="; a(2, 0, 3); a(2, 1, 3); a(2, 2, 3); a(2, 3, 3); a(2, 4, 3)
Print "  its neighbours are untouched: a(2,2,2)="; a(2, 2, 2); " a(1,2,3)="; a(1, 2, 3)

For i = 0 To 5
  c(i) = 700 + i
Next i
Math Insert a(), 1, 2, , c()
Print "  a(1,2,0..5) ="; a(1, 2, 0); a(1, 2, 1); a(1, 2, 2); a(1, 2, 3); a(1, 2, 4); a(1, 2, 5)

Print
Print "float arrays"
For i = 0 To 2
  For j = 0 To 3
    f(i, j) = i + j / 10.0
  Next j
Next i
Array Slice f(), 1, , g()
Print "  f(1,*) ="; g(0); g(1); g(2); g(3)
g(2) = 99.5
Array Insert f(), 1, , g()
Print "  after insert f(1,2) ="; f(1, 2); " f(0,2) ="; f(0, 2)

Print
Print "string arrays"
For i = 0 To 2
  For j = 0 To 3
    s$(i, j) = Chr$(65 + i) + Str$(j)
  Next j
Next i
Array Slice s$(), 2, , t$()
Print "  s$(2,*) = "; t$(0); " "; t$(1); " "; t$(2); " "; t$(3)
t$(1) = "zz"
Array Insert s$(), 2, , t$()
Print "  after insert s$(2,1) = "; s$(2, 1); "  s$(1,1) = "; s$(1, 1)

Print
Print "a slice of a whole array through a SUB, where the rank is not"
Print "known until it is called"
byrow(a(), 3)
Sub byrow(x%(), row%)
  Local integer v(5)
  Array Slice x%(), 3, row%, , v()
  Print "  x(3,"; Str$(row%); ",*) ="; v(0); v(1); v(2); v(3); v(4); v(5)
End Sub

Print
Print "COLOUR MAP - the array form of MAP()"
Dim integer codes(5), cout(5), pal(15)
For i = 0 To 5
  codes(i) = i * 3
Next i
Colour Map codes(), cout()
Print "  codes  ="; codes(0); codes(1); codes(2); codes(3); codes(4); codes(5)
For i = 0 To 5
  Print "  MAP("; Str$(codes(i)); ") = "; Hex$(cout(i), 6); "  MAP() agrees: "; Str$(cout(i) = Map(codes(i)))
Next i

Print "  with a palette of its own"
For i = 0 To 15
  pal(i) = i * &H010101
Next i
Colour Map codes(), cout(), pal()
For i = 0 To 5
  Print "  pal("; Str$(codes(i)); ") = "; Hex$(cout(i), 6)
Next i

Print "  in and out may be the same array"
Colour Map codes(), codes(), pal()
Print "  codes  = "; Hex$(codes(0), 6); " "; Hex$(codes(1), 6); " "; Hex$(codes(5), 6)

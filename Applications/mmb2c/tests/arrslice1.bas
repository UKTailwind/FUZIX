' ARRAY SLICE and ARRAY INSERT under OPTION BASE 1.
'
' The manual's own two examples, run verbatim, because this is where the
' element counts can go wrong.  They used to: our arrays were declared
' with bound + 1 elements whatever OPTION BASE said, so under BASE 1 an
' unreachable element 0 sat in every dimension and every count had to
' have it taken back off.  They are allocated dense now, exactly as
' MMBasic allocates them, so the two agree about the storage as well as
' the answer - and this test is what caught the last place that had not
' been told, the initialiser list on `sourcearray(4) = (1, 2, 3, 4)`.
Option Base 1
Dim integer targetarray(3, 4, 5)
Dim integer sourcearray(4) = (1, 2, 3, 4)
Dim integer a(3, 4, 5), b(4), i, j, k

Print "ARRAY INSERT - the manual's example"
Array Insert targetarray(), 2, , 3, sourcearray()
Print "  2,1,3="; targetarray(2, 1, 3); " 2,2,3="; targetarray(2, 2, 3);
Print " 2,3,3="; targetarray(2, 3, 3); " 2,4,3="; targetarray(2, 4, 3)
Print "  and nothing else was written: 1,1,3="; targetarray(1, 1, 3);
Print " 2,1,2="; targetarray(2, 1, 2)

Print
Print "ARRAY SLICE - the manual's example"
For i = 1 To 3
  For j = 1 To 4
    For k = 1 To 5
      a(i, j, k) = i * 100 + j * 10 + k
    Next k
  Next j
Next i
Array Slice a(), 2, , 3, b()
Print "  a(2,*,3) ="; b(1); b(2); b(3); b(4)

Print
Print "a size mismatch is MMBasic's own error"
Dim integer wrong(9)
On Error Skip 1
Array Slice a(), 2, , 3, wrong()
Print "  MM.ERRNO ="; MM.ErrNo; "  "; MM.ErrMsg$

Print
Print "COLOUR MAP's palette is 16 entries, counted the same way"
Dim integer codes(4), cout(4), pal(16)
For i = 1 To 4
  codes(i) = i * 4 - 4
  pal(i) = &H0F0F0F + i
Next i
For i = 5 To 16
  pal(i) = &H0F0F0F + i
Next i
Colour Map codes(), cout(), pal()
Print "  codes ="; codes(1); codes(2); codes(3); codes(4)
Print "  pal   = "; Hex$(cout(1), 6); " "; Hex$(cout(2), 6); " ";
Print Hex$(cout(3), 6); " "; Hex$(cout(4), 6)

Print
Print "and a palette of the wrong length is refused"
Dim integer short(4)
On Error Skip 1
Colour Map codes(), cout(), short()
Print "  MM.ERRNO ="; MM.ErrNo; "  "; MM.ErrMsg$

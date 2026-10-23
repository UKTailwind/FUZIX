' Under ON ERROR SKIP a write whose subscript is out of range writes
' nothing (FUZIX issue #26): findvar comes first.  BIT, BYTE and MID$
' wrote anyway, and READ took its value before it found its target - so
' the next READ got the DATA after the one that should have been left.
Dim a%(3) : a%(0) = 42
On Error Skip : Bit(a%(5), 0) = 1
Print "bit:"; a%(0)
Dim s$(3) : s$(0) = "peep"
On Error Skip : Byte(s$(5), 1) = 65
Print "byte: "; s$(0)
On Error Skip : Mid$(s$(5), 1, 1) = "z"
Print "mid: "; s$(0)
Data 10, 20
On Error Skip : Read a%(6)
Read b
Print "read:"; b
' findvar's messages and their order: every subscript against 32 bits
' and the base as it goes, then the tops - whichever subscript C
' evaluates first.
Dim m%(3, 3)
On Error Skip : m%(-1, 9) = 1
Print MM.ErrMsg$
On Error Skip : m%(9, -1) = 1
Print MM.ErrMsg$
On Error Skip : x = a%(-5000000000)
Print MM.ErrMsg$
On Error Skip : x = m%(1, 9)
Print MM.ErrMsg$

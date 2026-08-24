' Multi-dimensional array storage order.
'
' The layout is not an implementation detail: a program can read it with
' VARADDR, so it has to be MMBasic's.  Every number below comes from
' findvar's own offset arithmetic (MMBasic.c:4871-4878),
'
'     offset = i0 + i1*(d0+1) + i2*(d0+1)*(d1+1) + ...
'
' which makes the FIRST subscript the adjacent one.  Our C arrays are
' declared with their dimensions reversed so that this comes out the
' same; this test is what says so.
'
' Every line here is blessed against a real MMBasic 6.03.02, byte for
' byte - devtools/ab.py runs this file on the interpreter and diffs it
' against order.expected.  Re-run that before changing any number below.

Option Base 0

Dim Float c(5,5)
Dim Float d(2,5)
Dim Integer base

base = Peek(VarAddr c())
Print "c(0,0)"; (Peek(VarAddr c(0,0)) - base) / 8
Print "c(1,0)"; (Peek(VarAddr c(1,0)) - base) / 8
Print "c(0,1)"; (Peek(VarAddr c(0,1)) - base) / 8
Print "c(5,5)"; (Peek(VarAddr c(5,5)) - base) / 8

' Non-square, where getting it wrong shows up twice: the stride between
' c(0,j) and c(0,j+1) is the size of the FIRST dimension, not the second.
base = Peek(VarAddr d())
Print "d(1,0)"; (Peek(VarAddr d(1,0)) - base) / 8
Print "d(0,1)"; (Peek(VarAddr d(0,1)) - base) / 8
Print "d(2,5)"; (Peek(VarAddr d(2,5)) - base) / 8

' A flat sweep of the storage.  Filling with the first subscript
' innermost writes consecutive cells.
Dim Integer f(2,3)
Dim Integer i, j, k
k = 0
For j = 0 To 3
  For i = 0 To 2
    f(i,j) = k
    k = k + 1
  Next i
Next j
base = Peek(VarAddr f())
Print "flat";
For k = 0 To 11
  Print Peek(Integer base + k * 8);
Next k
Print

' READ fills an array in that same storage order (cmd_read walks the
' elements linearly), so the values land on these subscripts.
Dim Integer r(1,1)
Read r()
Print "read"; r(0,0); r(1,0); r(0,1); r(1,1)

' REDIM PRESERVE copies the old block's prefix and refuses to grow any
' but the last index - which preserves every element only because the
' last dimension's size multiplies no subscript.
'
' This line found a bug in the interpreter rather than in us.  MMBasic
' 6.03.02 as first tested printed 0 0 0 0 here, and went on to preserve
' nothing for one-dimensional, float and string arrays either; it was
' fixed in the reference, and the two now agree.  Worth remembering as
' the one place where "run it on a real PicoMite" answered a question
' about the PicoMite.
Dim Integer n = 1
Dim Integer p(n,n)
p(0,0) = 1
p(1,0) = 2
p(0,1) = 3
p(1,1) = 4
ReDim Preserve p(n,2)
Print "preserve"; p(0,0); p(1,0); p(0,1); p(1,1)

' A line taken along each dimension: same elements either way round, and
' the one thing here that was already right before the storage changed.
Dim Integer ln0(2), ln1(3)
Math Slice f(), , 1, ln0()
Print "slice0"; ln0(0); ln0(1); ln0(2)
Math Slice f(), 1, , ln1()
Print "slice1"; ln1(0); ln1(1); ln1(2); ln1(3)

' An array parameter has no rank of its own and folds out of the bounds
' table handed in beside it, which has to fold the same way.
Shown(f())

Data 10, 20, 30, 40

Sub Shown(x%())
  Local Integer a, b
  Print "param";
  For b = 0 To 3
    For a = 0 To 2
      Print x%(a,b);
    Next a
  Next b
  Print
  Print "bound"; Bound(x%(), 1); Bound(x%(), 2)
End Sub

' JSON$ with V8's path walker (PicoMite 9749c83, 10-21 #12): keys and
' [index]es in any order, so a path may start or end with an index, and
' a second index is read as written (it was read as 0); only the last
' key is matched regardless of case.
Option EXPLICIT
Dim Integer j(64), k(16)
L j(), "{'a':{'b':[10,20,{'c':'x','D':true}],'Name':'n1'},'arr':[[1,2],[3,4]],"
L j(), "'num':1.5,'big':12345678901,'neg':-3,'nul':null,'MixCase':{'Inner':7}}"
L k(), "[{'k':'v0'},{'k':'v1','L':[5,6]}]"
P j(), "a.b[1]" : P j(), "a.b[0]" : P j(), "a.b[2].c" : P j(), "a.b[2].d"
P j(), "arr[1][1]" : P j(), "arr[0][1]" : P j(), "arr[1]" : P j(), "arr[-1]"
P j(), "arr[ 1][+1]" : P j(), "num" : P j(), "big" : P j(), "neg" : P j(), "nul"
P j(), "mixcase.Inner" : P j(), "MixCase.inner" : P j(), "a.missing.b"
P j(), "a.b[2].c." : P j(), "a..b[1]" : P j(), "a.name" : P j(), "A.Name"
P k(), "[1].k" : P k(), "[1].K" : P k(), "[0].k" : P k(), "[1].L[1]" : P k(), "[2].k"
Print
End

Sub P(d() As Integer, path$)
  Print "[" JSON$(d(), path$) "]";
End Sub

Sub L(d() As Integer, s As String)
  Local Integer i
  For i = 1 To Len(s)
    If Mid$(s, i, 1) = Chr$(39) Then Mid$(s, i, 1) = Chr$(34)
  Next i
  LongString Append d(), s
End Sub

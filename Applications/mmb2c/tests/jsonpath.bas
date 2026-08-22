Option EXPLICIT
Dim Integer j(64)
L "{'Name':'Zoe','n':42,'f':2.5,'ok':true,'no':false,'z':null,"
L "'arr':[10,{'x':'y'},30],'obj':{'a':1,'b':2},"
L "'nl':'A" + Chr$(92) + "nB','e':1e2}"
Print "[" JSON$(j(), "Name") "]"
Print "[" JSON$(j(), "NAME") "]"
Print "[" JSON$(j(), "n") "][" JSON$(j(), "f") "][" JSON$(j(), "e") "]"
Print "[" JSON$(j(), "ok") "][" JSON$(j(), "no") "][" JSON$(j(), "z") "][" JSON$(j(), "nope") "]"
Print "[" JSON$(j(), "arr[1].x") "][" JSON$(j(), "arr[1].X") "]"
Print "[" JSON$(j(), "ARR[1].x") "][" JSON$(j(), "arr[0]") "][" JSON$(j(), "obj[1].a") "]"
Print Len(JSON$(j(), "nl")); Asc(Mid$(JSON$(j(), "nl"), 2, 1))
End

Sub L(s As String)
  Local Integer i
  For i = 1 To Len(s)
    If Mid$(s, i, 1) = Chr$(39) Then Mid$(s, i, 1) = Chr$(34)
  Next i
  LongString Append j(), s
End Sub

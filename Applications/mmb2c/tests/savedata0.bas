' SAVE DATA opens the file, emptying it, before it takes the length
' (FUZIX issue #39): with a length of 0 the file is left empty, so the
' LOAD after it changes nothing, and the error is "0 is invalid".  And
' LOAD DATA takes two arguments, SAVE DATA three - any other count is
' "Invalid syntax" when the line runs.
Dim a%(3) = (1, 2, 3, 4), b%(3)
Save Data "sd0.bin", Peek(VarAddr a%()), 32
On Error Skip : Save Data "sd0.bin", Peek(VarAddr a%()), 0 : Print MM.ErrMsg$
b%(0) = 9 : Load Data "sd0.bin", Peek(VarAddr b%())
Print b%(0)
On Error Skip : Load Data "sd0.bin", Peek(VarAddr b%()), 5 : Print MM.ErrMsg$
On Error Skip : Save Data "sd0.bin", Peek(VarAddr a%()) : Print MM.ErrMsg$
Kill "sd0.bin"
Print "end"

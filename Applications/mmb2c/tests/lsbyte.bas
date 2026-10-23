' LONGSTRING SETBYTE and LGETBYTE take a position in the array's
' capacity, with getint's message, and SETBYTE never lengthens the
' string (FUZIX issue #11; the board cases are the issue's).
Dim s%(4)
LongString Append s%(), "hello"
On Error Skip 1
LongString SetByte s%(), -1, 65
Print MM.ErrMsg$; LLen(s%())
LongString SetByte s%(), 10, 65
Print LLen(s%()); LGetByte(s%(), 10); LGetByte(s%(), 4)
LongString SetByte s%(), 31, 66
Print LGetByte(s%(), 31)
On Error Skip 1
LongString SetByte s%(), 32, 65
Print MM.ErrMsg$
On Error Skip 1
LongString SetByte s%(), 0, 256
Print MM.ErrMsg$
On Error Skip 1
x = LGetByte(s%(), 33)
Print MM.ErrMsg$
On Error Skip 1
x = LGetByte(s%(), -1)
Print MM.ErrMsg$
Print LLen(s%())

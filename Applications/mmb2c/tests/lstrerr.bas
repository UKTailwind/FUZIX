' LONGSTRING's arguments are the reference's getints (MM_Misc.c, 8d44510
' issue #72): a negative count is an error rather than 0, MID's start
' must lie in the string, REPLACE writes over what is there and never
' grows it, RESIZE stays inside the array, SETBYTE takes 0..255, LMID's
' start and count lie in the string - and LINSTR from a start below 1
' finds nothing.  Each "too small" carries the reference's own wording.
Dim a%(4), b%(4), c%(1)
LongString Load a(), 5, "hello"
ON ERROR SKIP 1
LongString Load b(), -1, "x"
Print "load -1: " MM.ERRMSG$
ON ERROR SKIP 1
LongString Left b(), a(), -2
Print "left -2: " MM.ERRMSG$
ON ERROR SKIP 1
LongString Right b(), a(), -3
Print "right -3: " MM.ERRMSG$
ON ERROR SKIP 1
LongString Mid b(), a(), 0
Print "mid 0: " MM.ERRMSG$
ON ERROR SKIP 1
LongString Mid b(), a(), 6
Print "mid 6: " MM.ERRMSG$
LongString Mid b(), a(), 2, 3
Print "mid 2,3: "; LGetStr$(b(), 1, LLen(b()))
LongString Replace a(), "EL", 2
Print "replace 2: "; LGetStr$(a(), 1, LLen(a())); " len"; LLen(a())
ON ERROR SKIP 1
LongString Replace a(), "XY", 5
Print "replace 5: " MM.ERRMSG$; ", still "; LGetStr$(a(), 1, LLen(a()))
Print "linstr 0: "; LInstr(a(), "L", 0); "  linstr 1: "; LInstr(a(), "L", 1)
ON ERROR SKIP 1
LongString Resize a(), 40
Print "resize 40: " MM.ERRMSG$
ON ERROR SKIP 1
LongString Setbyte a(), 0, 256
Print "setbyte 256: " MM.ERRMSG$
ON ERROR SKIP 1
LMid(a(), 0, 1) = "z"
Print "lmid 0: " MM.ERRMSG$
ON ERROR SKIP 1
LongString Append c(), "123456789"
Print "append: " MM.ERRMSG$
LongString Append a(), "world!"
ON ERROR SKIP 1
LongString Copy c(), a()
Print "copy 11 into 8: " MM.ERRMSG$

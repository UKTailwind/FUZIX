' BIT, BYTE, FLAG and LMID - the assignment forms.
'
' Each of these reaches INTO something rather than replacing it, which
' is why they are statements that look like functions.  The reading
' forms are ordinary functions and are checked here against the writes.
Dim integer v, i
Dim s$, t$
Dim integer ls(20)

Print "BIT"
v = 0
Bit(v, 0) = 1
Bit(v, 3) = 1
Bit(v, 63) = 1
Print "  set 0,3,63     v = "; Hex$(v, 16)
Print "  read back      "; Bit(v, 0); Bit(v, 1); Bit(v, 3); Bit(v, 63)
Bit(v, 3) = 0
Print "  cleared 3      v = "; Hex$(v, 16)
v = 0
For i = 0 To 7
  Bit(v, i) = 1
Next i
Print "  bits 0-7       v = "; v; " (255 expected)"
Print

Print "BYTE"
s$ = "hello"
Byte(s$, 1) = Asc("H")
Byte(s$, 5) = Asc("O")
Print "  s$ = "; s$; "   len="; Len(s$)
Print "  Byte(s$,1) = "; Byte(s$, 1); "  Byte(s$,5) = "; Byte(s$, 5)
Print

Print "FLAG"
Flags = 0
Flag(2) = 1
Flag(5) = 1
Print "  flags 2,5      "; Flag(0); Flag(2); Flag(5); "  MM.INFO(FLAGS) = "; MM.Info(Flags)
Flag(2) = 0
Print "  cleared 2      MM.INFO(FLAGS) = "; MM.Info(Flags)
Flags = &HFF
Print "  FLAGS = &HFF   Flag(7) = "; Flag(7); "  Flag(8) = "; Flag(8)
Flags = 0
Print

Print "LMID"
LongString Clear ls()
LongString Append ls(), "abcdefghij"
Print "  start          "; LGetStr$(ls(), 1, LLen(ls())); "   len="; LLen(ls())
' same length: a straight overwrite
LMid(ls(), 4, 3) = "XYZ"
Print "  LMid(,4,3)=XYZ "; LGetStr$(ls(), 1, LLen(ls())); "   len="; LLen(ls())
' longer replacement: the tail moves right and the string grows
LMid(ls(), 4, 3) = "12345"
Print "  LMid(,4,3)=12345 "; LGetStr$(ls(), 1, LLen(ls())); " len="; LLen(ls())
' shorter replacement: the tail moves left and the string shrinks
LMid(ls(), 4, 5) = "-"
Print "  LMid(,4,5)=-   "; LGetStr$(ls(), 1, LLen(ls())); "   len="; LLen(ls())
' num omitted: as long as the replacement, so an overwrite
LMid(ls(), 1) = "AB"
Print "  LMid(,1)=AB    "; LGetStr$(ls(), 1, LLen(ls())); "   len="; LLen(ls())

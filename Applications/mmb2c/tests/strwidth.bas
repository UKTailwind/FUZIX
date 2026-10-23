' STR$'s width and decimals as fun_str takes them (FUZIX issue #37):
' getint(m, -128, 128) and getint(n, -20, 20), checked in that order and
' then the pad, whose first character counts to seven bits.  The widest
' text they allow is 149 characters, which ran off a 129-byte buffer.
Print Len(Str$(Val("5"), 128, 20)); Len(Str$(5, 128, 20))
Print Len(Str$(5.5, -128, 20)); Len(Str$(-Val("1.5"), 128, -20))
Print Str$(5, 4, 0, Chr$(200))
On Error Skip : a$ = Str$(5, 200) : Print MM.ErrMsg$
On Error Skip : a$ = Str$(5, 0, 21) : Print MM.ErrMsg$
On Error Skip : a$ = Str$(5, 0, 2, "") : Print MM.ErrMsg$
On Error Skip : a$ = Str$(5, -200, 21) : Print MM.ErrMsg$
On Error Skip : a$ = Str$(Val("5"), 0, -21) : Print MM.ErrMsg$
Print "end"

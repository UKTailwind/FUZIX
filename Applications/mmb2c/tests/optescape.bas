' OPTION ESCAPE - the tokenizer decodes literals positionally: this
' one, before the statement, stays raw.
Dim raw$ = "a\nb"
Option Escape : Print "same:["; Len("x\ty"); "]"
Print Len(raw$); ":"; raw$
Print "nl:"; Len("x\ny")
Print "cr:"; Len("a\rb")
Print "quote:[\q]"
Print "backslash:[\\]"
Print "bell:"; Asc("\a")
Print "bs:"; Asc("\b")
Print "esc:"; Asc("\e")
Print "ff:"; Asc("\f")
Print "vt:"; Asc("\v")
Print "dec:"; "\065\066\067"
Print "dechigh:"; Asc("\200")
Print "wrap:"; Asc("\999")
Print "hex:"; "\&41\&42"
Print "hexcase:"; "\&6a\&4B"
Print "twodigits:[\65]"
Print "onehex:[\&4]"
Print "unknown:[\z]"
Print "upper:[\N]"
Print "trail:["; "x\"; "]"
End

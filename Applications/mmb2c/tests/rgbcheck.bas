' RGB() as fun_rgb: three components each getint(..., 0, 255) - out of
' range is the range error, not masked - or one argument that must be a
' colour name (LIGHTGRAY, not LITEGRAY); any other single argument is
' "Invalid colour: <it>", raised when the line runs.
Print Hex$(RGB(1, 2, 3)); " "; Hex$(RGB(LIGHTGRAY)); " "; Hex$(RGB(red))
x% = 128
Print Hex$(RGB(x%, x%, x%)); " "; Hex$(RGB(255, x% - 128, 0))
r% = 300
ON ERROR SKIP 1
c% = RGB(r%, 0, 0)
Print "300: " MM.ERRMSG$
g% = -1
ON ERROR SKIP 1
c% = RGB(0, g%, 0)
Print "-1: " MM.ERRMSG$
ON ERROR SKIP 1
c% = RGB(LITEGRAY)
Print "LITEGRAY: " MM.ERRMSG$
ON ERROR SKIP 1
c% = RGB(PURPLE)
Print "PURPLE: " MM.ERRMSG$
If 0 Then c% = RGB(NOSUCH)
Print "not run: no error"

' VAL as fun_val and StrVal: an integer string is an integer, so where
' the value is used as an integer it is exact; a float string rounds as
' an integer assignment rounds it.  &H/&O/&B only as the first character.
a% = Val("9007199254740993")
Print "exact "; a%
Print "hex "; Hex$(Val("&H7FFFFFFFFFFFFFFF"))
b% = Val("1.5") : c% = Val("-2.5")
Print "round "; b%; c%
Print "and "; Val("&HFF") And 15
Print "big "; Val("99999999999999999999") And 1
d% = Val("-99999999999999999999")
Print "clamp "; d%
Print "space "; Val(" &H10"); Val("&H10"); Val("  42")
Print "wrap "; Val("&HFFFFFFFFFFFFFFFFF")
Print "bases "; Val("&O17"); Val("&B101"); Val("&"); Val("&X12")
Print "float "; Val("1e3"); Val("12abc"); Val("abc"); Val("1."); Val(".5")
Print "sum "; Val("123") + 0.5
e% = Cint(Val("9007199254740993"))
Print "cint "; e%

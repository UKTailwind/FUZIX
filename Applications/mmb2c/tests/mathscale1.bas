' MATH SCALE by 1 is a plain copy, as cmd_math's scale != 1.0 test
' makes it, so an integer array past 2^53 keeps every digit (FUZIX
' issue #5).  Any other float scale is the rounded product.
Dim a%(1), b%(1)
a%(0) = 9223372036854775807 : a%(1) = 9007199254740993
Math Scale a%(), 1.0, b%()
Print b%(0); b%(1)
Math Scale a%(), 1, b%()
Print b%(0); b%(1)
a%(0) = 5 : a%(1) = 7
Math Scale a%(), 0.3, b%()
Print b%(0); b%(1)

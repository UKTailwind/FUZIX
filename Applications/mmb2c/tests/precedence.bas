' Operator precedence as the reference's doexpr and getvalue apply it
' (core/MMBasic.c; the table is AllCommands.h): equal precedence runs
' left to right, = is below the other comparisons, NOT and INV take the
' next value alone, and a minus takes the powers after it first.
Print "2^3^2 "; 2^3^2
Print "-2^2 "; -2^2
Print "-2^3^2 "; -2^3^2
Print "-2^2*3 "; -2^2*3
Print "4^-1^2 "; 4^-1^2
Print "2^-1 "; 2^-1
Print "NOT 0 + 1 "; NOT 0 + 1
Print "NOT 0 * 5 "; NOT 0 * 5
Print "INV 0 + 1 "; INV 0 + 1
Print "INV 1 ^ 2 "; INV 1 ^ 2
Print "2 + NOT 1 "; 2 + NOT 1
Print "NOT 1 = 0 "; NOT 1 = 0
Print "NOT NOT 3 "; NOT NOT 3
Print "-NOT 0 "; -NOT 0
Print "2 = 1 < 3 "; 2 = 1 < 3
Print "0 < 1 = 1 "; 0 < 1 = 1
Print "1 <> 2 = 1 "; 1 <> 2 = 1
Print "3 - 2 - 1 "; 3 - 2 - 1
Print "16 / 4 / 2 "; 16 / 4 / 2
Print "2 * 3 MOD 4 "; 2 * 3 MOD 4
Print "1 << 2 + 1 "; 1 << 2 + 1
a = 3 : b = 3
If NOT a = b Then Print "not-eq wrong" Else Print "NOT a = b is (NOT a) = b"

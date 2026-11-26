' FORMAT$ as V8.00.00b3 has it (format_float): the digits are the
' value's own, worked out exactly, and a tie goes to the even digit.
' It used to build them with FloatToStr's arithmetic, which ran out
' of digits after 15 or so, rounded 0.125 up and 0.375 down, went
' wrong past 2^63 and printed INFe-2147483647.  The default precision
' is 4, not C's 6, and a precision past 30 is taken as 30.

DIM big! = 1E300, inf! = 1E308 * 10, tiny! = 2.5E-10
PRINT FORMAT$(-tiny!, "%.17g")
PRINT FORMAT$(big!, "%.17g")
PRINT FORMAT$(0.1, "%.17g")
PRINT FORMAT$(0.1, "%.30f")
PRINT FORMAT$(0.1, "%.40f")
PRINT FORMAT$(1 / 3, "%.20f")
PRINT FORMAT$(123456789.125, "%.20g")
PRINT FORMAT$(4.9406564584124654E-324, "%.3e")

' ties
PRINT FORMAT$(0.125, "%.2f"); " "; FORMAT$(0.375, "%.2f"); " ";
PRINT FORMAT$(2.5, "%.0f"); " "; FORMAT$(3.5, "%.0f"); " "; FORMAT$(12345, "%g")

' the default precision
PRINT FORMAT$(1E22, "%f")
PRINT FORMAT$(PI, "%f"); " "; FORMAT$(PI, "%e"); " "; FORMAT$(PI, "%g")
PRINT FORMAT$(1E-5, "%g"); " "; FORMAT$(1E-4, "%g"); " "; FORMAT$(1000, "%g"); " "; FORMAT$(100000, "%g")
PRINT FORMAT$(1.5E-300, "%E"); " "; FORMAT$(0, "%+.3e"); " "; FORMAT$(7, "%G")

' widths and flags
PRINT "["; FORMAT$(-3.14159, "%010.2f"); "]["; FORMAT$(3.14159, "%-10.2f"); "][";
PRINT FORMAT$(12345.678, "%12.3e"); "]["; FORMAT$(42, "% .1f"); "]["; FORMAT$(42, "%+g"); "]"

' infinities, never zero-padded
PRINT FORMAT$(inf!, "%g"); " "; FORMAT$(-inf!, "%G"); " ["; FORMAT$(inf!, "%10.3f"); "][";
PRINT FORMAT$(-inf!, "%010F"); "]["; FORMAT$(inf!, "%-6e"); "]"

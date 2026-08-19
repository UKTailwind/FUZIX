' TEXT and FONT - what can be checked without a screen.
'
' The host build has no display, so nothing is drawn and mm_fontinfo
' says so; what this gate holds is that every argument form PARSES, that
' the optional arguments may be left out singly or in runs, and that a
' program full of TEXT still runs to the end rather than dying on a font
' it cannot have.  The drawing itself is checked on hardware by reading
' the pixels back - samples/fbfont.bas.

Option Explicit
Option Default Float
Dim Integer i
Dim s$

Print "start"

' Every optional argument absent
Text 0, 0, "plain"

' ... and present, in turn
Text 10, 20, "just", "CM"
Text 10, 20, "font", "LT", 3
Text 10, 20, "scale", "LT", 1, 2
Text 10, 20, "fg", "LT", 1, 1, &HFF0000
Text 10, 20, "bg", "LT", 1, 1, &HFF0000, &H0000FF
Text 10, 20, "clear", "LT", 1, 1, &HFF0000, -1

' Bare commas: an omitted argument in the middle
Text 10, 20, "skip1", , 4
Text 10, 20, "skip2", , , 3
Text 10, 20, "skip3", "RB", , , &H00FF00

' Expressions, not just literals, in every position
i = 2
s$ = "expr"
Text i * 10, i + 5, s$ + "!", "C" + "T", i, i, i * 100, -1

' Every justification letter the parser accepts
Text 0, 0, "a", "L"
Text 0, 0, "b", "C"
Text 0, 0, "c", "R"
Text 0, 0, "d", "LT"
Text 0, 0, "e", "CM"
Text 0, 0, "f", "RB"
Text 0, 0, "g", "LTN"
Text 0, 0, "h", "CMV"
Text 0, 0, "i", "RBI"
Text 0, 0, "j", "LTU"
Text 0, 0, "k", "CMD"
Text 0, 0, "l", ""

' Lower case is accepted too
Text 0, 0, "m", "cm"

' CLS with and without a colour, and with an expression.  With no
' display these send the console an ANSI clear, which is why the
' expected output has escapes in it.
Cls
Cls &H0000FF
Cls i * 2

' MAP: every statement form, the function, and an expression index
Map(0) = &H000000
Map(15) = Rgb(WHITE)
Map(i) = i * 16
Map Set
Map Reset
Map Maximite
Map Greyscale
Print Hex$(Map(0), 6); " "; Hex$(Map(8), 6); " "; Hex$(Map(15), 6)

' FONT, with and without the # and the scale
Font 1
Font #1
Font 2, 3
Font #9, 1

' A long string, to be sure the run length is carried through
s$ = ""
For i = 1 To 40
  s$ = s$ + "x"
Next i
Text 0, 0, s$, "LT", 1, 1

Print "end"

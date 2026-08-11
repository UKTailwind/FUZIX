' MM.INFO(FONT ADDRESS n) and PEEK - reading the built-in fonts.
'
' The fonts are the kernel's, in flash, and there is no MMU here, so
' the address it hands back is one this program can simply read.  That
' is what lets a program draw MMBasic's own glyphs onto something the
' kernel has never heard of - an SPI panel, say - instead of shipping
' a second copy of the font.
'
' The header is the first four bytes: width, height, first character,
' count.  Nothing below assumes 8x12; it all comes out of the font.

Option Explicit
Option Default Integer

Dim a, w, h, first, count, f
Dim g, row, col, bit, byt, ink
Dim line$

' Address 0 means there is no display - the host build, where the gates
' run.  Check before reading: a wrong address here is not an error
' message, it is a fault, exactly as it would be in C.
a = MM.INFO(FONT ADDRESS 1)
If a = 0 Then
  Print "no display, so no fonts to read"
  End
EndIf

Print "font  address     cell   first  count"
For f = 1 To 9
  a = MM.INFO(FONT ADDRESS f)
  If a <> 0 Then
    w = Peek(BYTE a)
    h = Peek(BYTE a + 1)
    first = Peek(BYTE a + 2)
    count = Peek(BYTE a + 3)
    Print f, Hex$(a, 8), Str$(w) + "x" + Str$(h), first, count
  EndIf
Next f

' Now draw one glyph as text, straight out of the flash.  Font 3 is the
' 16x24 the HDMI console uses.
a = MM.INFO(FONT ADDRESS 3)
w = Peek(BYTE a)
h = Peek(BYTE a + 1)
first = Peek(BYTE a + 2)

' The glyph for character c starts at addr + 4 + (c - first) * w * h / 8,
' packed MSB first with no padding between rows - MMBasic's layout.
g = a + 4 + (Asc("A") - first) * w * h \ 8

Print
Print "'A' from font 3, "; Str$(w); "x"; Str$(h); ", read out of flash:"
For row = 0 To h - 1
  line$ = ""
  For col = 0 To w - 1
    bit = row * w + col
    byt = Peek(BYTE g + bit \ 8)
    ink = (byt >> (7 - (bit And 7))) And 1
    If ink Then line$ = line$ + "#" Else line$ = line$ + "."
  Next col
  Print line$
Next row

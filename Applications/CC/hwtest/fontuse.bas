' Is a user-defined font actually the one being drawn with?
'
' fontdef.bas proves DefineFont translates and does not error; it never
' proves a glyph came from it.  picofrog's panel came out overlapping -
' which is what a built-in font looks like where an 8x8 one was asked
' for - so this asks the screen instead of the translator.
'
' Font 10 here is three deliberately unmistakable 8x8 glyphs:
'   "0" solid   - all 64 pixels ink
'   "1" checker - 32 of 64
'   "2" bar     - a vertical stripe
' No built-in font looks like any of them, so a count settles it.
'
' Results go to a FILE.  After MODE 2 a PRINT is drawn on the display
' and never reaches stdout, so a test that printed its findings had
' nothing to say to anyone reading over the serial line.

  Option BASE 0
  Option DEFAULT NONE
  Option EXPLICIT ON
  Dim x%, y%, n%

  Mode 2
  CLS
  Open "fontuse.out" For Output As #1

  ' ---- 1. does PIXEL() read back at all?  A filled box is the
  ' simplest thing that can be true; if this is 0 nothing below means
  ' anything.
  Box 0,0,8,8,,RGB(white),RGB(white)
  n%=0
  For y%=0 To 7 : For x%=0 To 7
    If Pixel(x%,y%)<>0 Then Inc n%
  Next x% : Next y%
  Print #1, "1 box readback   : "; n%; "  (want 64)"

  ' ---- 2. does TEXT draw at all, in the built-in font?
  CLS
  Text 0,0,"0",,1,1,RGB(white),RGB(black)
  n%=0
  For y%=0 To 15 : For x%=0 To 15
    If Pixel(x%,y%)<>0 Then Inc n%
  Next x% : Next y%
  Print #1, "2 font 1 '0'     : "; n%; "  (want > 0)"

  ' ---- 3. the user font, named on the TEXT itself
  CLS
  Text 0,0,"0",,10,1,RGB(white),RGB(black)
  n%=0
  For y%=0 To 7 : For x%=0 To 7
    If Pixel(x%,y%)<>0 Then Inc n%
  Next x% : Next y%
  Print #1, "3 font 10 arg '0': "; n%; "  (want 64 - solid)"

  ' ---- 4. the user font, selected with FONT as picofrog does
  CLS
  Font 10
  Text 0,0,"0",,,1,RGB(white),RGB(black)
  n%=0
  For y%=0 To 7 : For x%=0 To 7
    If Pixel(x%,y%)<>0 Then Inc n%
  Next x% : Next y%
  Print #1, "4 FONT 10 then   : "; n%; "  (want 64 - solid)"

  ' ---- 5. the checkerboard, so a wrong-but-present glyph is not
  ' mistaken for the right one
  CLS
  Text 0,0,"1",,,1,RGB(white),RGB(black)
  n%=0
  For y%=0 To 7 : For x%=0 To 7
    If Pixel(x%,y%)<>0 Then Inc n%
  Next x% : Next y%
  Print #1, "5 checker '1'    : "; n%; "  (want 32)"

  ' ---- 6. the advance: two glyphs 8 apart, or the built-in width?
  CLS
  Text 0,0,"00",,,1,RGB(white),RGB(black)
  n%=0
  For x%=0 To 31
    If Pixel(x%,0)<>0 Then Inc n%
  Next x%
  Print #1, "6 two glyphs row0: "; n%; "  (want 16)"

  Font 1
  Print #1, "font address 1 : "; MM.INFO(FONT ADDRESS 1)
  Print #1, "font address 10: "; MM.INFO(FONT ADDRESS 10)
  Close #1
  End

DefineFont 10
  03300808
  FFFFFFFF FFFFFFFF
  55AA55AA 55AA55AA
  18181818 00180018
End DefineFont

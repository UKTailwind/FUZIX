' QNH station - a worked example of most of what the PC3 can reach.
'
'   an ILI9341 panel on SPI0          a BMP180 barometer on I2C2
'   a potentiometer on the ADC        the DS3231 clock, via TIME$/DATE$
'   PWM for the backlight             MMBasic's own fonts, drawn by hand
'
' It shows the time, the date, the temperature, the station pressure
' and - set by the potentiometer - the height above sea level, from
' which it computes QNH: the pressure the sensor would read if it were
' at sea level, which is the number an altimeter is set to.
'
' Wiring
'   GP2  SCLK    GP3  MOSI   GP4  MISO      the panel's SPI
'   GP5  DC      GP6  RESET  GP7  CS        the panel's control lines
'   GP0  LED     (backlight, through its transistor, on PWM slice 0)
'   GP38 SDA     GP39 SCL                   I2C2, the BMP180 at &H77
'   GP41         the potentiometer wiper    (ADC channel 1)
'
' The only part of this the firmware does for you is SPI, I2C2, PWM,
' the ADC and the clock.  The panel driver, the glyph rendering and the
' barometry are all BASIC, below.

Option Explicit
Option Default Float

' ---- pins -----------------------------------------------------------
Const SCKPIN = 2, MOSIPIN = 3, MISOPIN = 4
Const DCPIN = 5, RSTPIN = 6, CSPIN = 7
Const BLPIN = 0
Const POTPIN = 41
Const SDAPIN = 38, SCLPIN = 39
Const BMPADDR = &H77

Const SW = 240, SH = 320             ' the panel, portrait

' ---- colours, RGB565 -------------------------------------------------
Const BLACK = &H0000, WHITE = &HFFFF, GREY = &H8410
Const YELLOW = &HFFE0, CYAN = &H07FF, GREEN = &H07E0
Const ORANGE = &HFD20, SKY = &H045F

' ---- the ceiling the pot sets, in metres ----------------------------
Const MAXALT = 1000

' ---- fonts.  Cached once: address, cell and range, straight out of
'      each font's own four-byte header.  Nothing below assumes a size.
Dim Integer fadr(9), fwid(9), fhgt(9), ffst(9), fcnt(9)

' ---- BMP180 calibration and working values --------------------------
Dim i2cin$ length 32
Dim Integer UT, UP, OSS
Dim Integer ac1, ac2, ac4, ac5, ac6, b1, b2, mb, mc, md
Dim ac3, x3
Dim Integer x1, x2, b5, b6, b3, b4, b7
Dim Integer temperature, pressure
Dim Integer OSSdata(4), OSSscale(4)

' ---- what is on screen now, so only what changed is redrawn ---------
Dim last$(8) length 24
Dim Integer i, alt, lastalt
Dim tC, pHPa, qnh, t0

' ======================= start up ====================================

Print "QNH station starting"
setup_display()
setup_sensor()
setup_pot()
load_fonts()
layout()

' ======================= the loop ====================================
'
' Once a second, and asleep in between: PAUSE really does yield the
' processor here rather than spinning, so the machine is free for
' anything else running on it.

Do
  t0 = Timer

  ' the clock, from the DS3231 the kernel keeps the system time from
  show(0, Left$(Time$, 8), 5, WHITE)
  show(1, Date$, 3, SKY)

  ' the sensor
  read_bmp180()
  tC = temperature / 10
  pHPa = pressure / 100
  show(2, Str$(tC, 0, 1) + " C", 3, ORANGE)
  show(3, Str$(pHPa, 0, 1) + " hPa", 3, YELLOW)

  ' the potentiometer, rounded to a metre and only acted on when it
  ' really moved - one ADC count is about a quarter of a metre here,
  ' and a display that flickers between 141 and 142 is worse than one
  ' that is a metre out.
  alt = Int(Pin(POTPIN) / 3.3 * MAXALT + 0.5)
  If alt < 0 Then alt = 0
  If alt > MAXALT Then alt = MAXALT
  If Abs(alt - lastalt) >= 2 Then lastalt = alt
  show(4, Str$(lastalt) + " m", 3, GREEN)

  ' QNH: the station pressure reduced to sea level.  This is the
  ' inverse of the standard altitude formula in the BMP180 datasheet
  ' (section 3.6), which is the one Bosch publishes and the one every
  ' weather station uses:
  '
  '     p0 = p / (1 - altitude / 44330) ^ 5.255
  '
  ' At 150 m that is about 18 hPa, which is the difference between a
  ' storm and a fine day if you read it off the sensor unreduced.
  qnh = pHPa / (1 - lastalt / 44330.0) ^ 5.255
  show(5, Str$(qnh, 0, 1), 5, CYAN)

  ' whatever is left of the second
  Do While Timer - t0 < 1000
    Pause 20
  Loop
Loop

' ======================= the display =================================

Sub setup_display
  Local Integer got

  SetPin DCPIN, DOUT
  SetPin RSTPIN, DOUT
  SetPin CSPIN, DOUT
  Pin(CSPIN) = 1
  Pin(DCPIN) = 1

  ' The backlight is transistor driven, so duty is brightness and 0 is
  ' off.  Slice 0 is GP0's.
  SetPin BLPIN, PWM
  PWM 0, 1000, 70

  ' Any order: the pin number decides which signal each carries.
  SetPin SCKPIN, MOSIPIN, MISOPIN, SPI
  SPI Open 62500000, 0, 8
  got = MM.SPISPEED
  Print "SPI at "; got \ 1000000; "."; (got \ 100000) Mod 10; " MHz"

  ' MMBasic's own reset timing, out of SPI-LCD.c
  Pin(RSTPIN) = 1 : Pause 10
  Pin(RSTPIN) = 0 : Pause 10
  Pin(RSTPIN) = 1 : Pause 200

  cmd(&H01) : Pause 20                 ' software reset
  cmd(&H28)                            ' display off
  cmd1(&HC0, &H23)                     ' power control 1
  cmd1(&HC1, &H10)                     ' power control 2
  cmd2(&HC5, &H2B, &H2B)               ' VCOM control 1
  cmd1(&HC7, &HC0)                     ' VCOM control 2
  cmd1(&H3A, &H55)                     ' 16 bits per pixel
  cmd2(&HB1, &H00, &H1B)               ' frame control
  cmd1(&HB7, &H07)                     ' entry mode
  cmd1(&H11, &H00) : Pause 50          ' sleep out
  cmd(&H13)                            ' normal display
  cmd(&H29) : Pause 100                ' display on
  cmd1(&H36, &H48)                     ' portrait, BGR order
  fillrect(0, 0, SW, SH, BLACK)
End Sub

Sub cmd(c As Integer)
  Pin(CSPIN) = 0 : Pin(DCPIN) = 0
  SPI Write 1, c
  Pin(CSPIN) = 1
End Sub

Sub cmd1(c As Integer, d1 As Integer)
  Pin(CSPIN) = 0 : Pin(DCPIN) = 0
  SPI Write 1, c
  Pin(DCPIN) = 1
  SPI Write 1, d1
  Pin(CSPIN) = 1
End Sub

Sub cmd2(c As Integer, d1 As Integer, d2 As Integer)
  Pin(CSPIN) = 0 : Pin(DCPIN) = 0
  SPI Write 1, c
  Pin(DCPIN) = 1
  SPI Write 2, d1, d2
  Pin(CSPIN) = 1
End Sub

' The drawing window, left with the chip expecting pixel data and CS
' still low - the caller writes the pixels and raises CS.
Sub setwin(x0 As Integer, y0 As Integer, x1 As Integer, y1 As Integer)
  Pin(CSPIN) = 0 : Pin(DCPIN) = 0
  SPI Write 1, &H2A
  Pin(DCPIN) = 1
  SPI Write 4, x0 >> 8, x0 And 255, x1 >> 8, x1 And 255
  Pin(DCPIN) = 0
  SPI Write 1, &H2B
  Pin(DCPIN) = 1
  SPI Write 4, y0 >> 8, y0 And 255, y1 >> 8, y1 And 255
  Pin(DCPIN) = 0
  SPI Write 1, &H2C
  Pin(DCPIN) = 1
End Sub

' A solid rectangle.  A BASIC string caps at 255 bytes, so the run is
' built once as 100 pixels and repeated - the kernel has no such limit,
' only the buffer this side does.
Sub fillrect(x As Integer, y As Integer, w As Integer, h As Integer, c As Integer)
  Local run$, px$
  Local Integer n, total, chunk

  px$ = Chr$(c >> 8) + Chr$(c And 255)
  run$ = ""
  For n = 1 To 100
    run$ = run$ + px$
  Next n
  setwin(x, y, x + w - 1, y + h - 1)
  total = w * h
  Do While total > 0
    chunk = 100
    If chunk > total Then chunk = total
    SPI Write chunk * 2, Left$(run$, chunk * 2)
    total = total - chunk
  Loop
  Pin(CSPIN) = 1
End Sub

' ======================= the fonts ===================================
'
' MM.INFO(FONT ADDRESS n) gives the machine address of font n's data.
' There is no MMU on this machine, so that address is one this program
' can simply read, and the fonts are const so they sit in flash and
' never move.  The first four bytes are the font's own header:
'
'   width, height, first character, how many characters
'
' and the glyph for character c starts at
'
'   address + 4 + (c - first) * width * height / 8
'
' packed MSB first with no padding between rows.  That is MMBasic's
' layout, unchanged - these are MMBasic's nine fonts.

Sub load_fonts
  Local Integer f, a

  For f = 1 To 9
    a = MM.INFO(FONT ADDRESS f)
    fadr(f) = a
    If a <> 0 Then
      fwid(f) = Peek(BYTE a)
      fhgt(f) = Peek(BYTE a + 1)
      ffst(f) = Peek(BYTE a + 2)
      fcnt(f) = Peek(BYTE a + 3)
    EndIf
  Next f
  If fadr(3) = 0 Then Error "no fonts - is this a PC3?"
  Print "font 3 is "; Str$(fwid(3)); "x"; Str$(fhgt(3));
  Print ", font 5 is "; Str$(fwid(5)); "x"; Str$(fhgt(5))
End Sub

' One character, into a window of exactly its own cell.  The window is
' set once and the panel advances by itself, so a glyph costs one
' address round trip and one write per row.
Sub drawchar(x As Integer, y As Integer, c As Integer, f As Integer, fg As Integer, bg As Integer)
  Local Integer w, h, g, row, col, bidx, bval, rowbit
  Local px$, ink$, paper$

  w = fwid(f) : h = fhgt(f)
  ink$ = Chr$(fg >> 8) + Chr$(fg And 255)
  paper$ = Chr$(bg >> 8) + Chr$(bg And 255)

  ' A character the font does not have prints as a blank cell, which is
  ' what MMBasic does - and with font 6, which is the digits only, that
  ' is every letter.
  If c < ffst(f) Or c >= ffst(f) + fcnt(f) Then
    fillrect(x, y, w, h, bg)
    Exit Sub
  EndIf

  g = fadr(f) + 4 + (c - ffst(f)) * w * h \ 8
  setwin(x, y, x + w - 1, y + h - 1)
  For row = 0 To h - 1
    px$ = ""
    rowbit = row * w
    For col = 0 To w - 1
      bidx = rowbit + col
      bval = Peek(BYTE g + bidx \ 8)
      If (bval >> (7 - (bidx And 7))) And 1 Then
        px$ = px$ + ink$
      Else
        px$ = px$ + paper$
      EndIf
    Next col
    SPI Write w * 2, px$
  Next row
  Pin(CSPIN) = 1
End Sub

' A glyph that would hang over the right edge is DROPPED, not drawn.
' The panel clamps a window to its own width but still takes every
' pixel written into it, so the surplus wraps onto the next row and the
' character comes out as garbage - which is what a 24-pixel font did
' with the tenth character of a 240-pixel line.  Silently short is a
' layout mistake you can see; silently wrapped looks like a bug in the
' font reader.
Sub drawstr(x As Integer, y As Integer, s$, f As Integer, fg As Integer, bg As Integer)
  Local Integer i, w, gx

  w = fwid(f)
  For i = 1 To Len(s$)
    gx = x + (i - 1) * w
    If gx + w > SW Then Exit For
    drawchar(gx, y, Asc(Mid$(s$, i, 1)), f, fg, bg)
  Next i
End Sub

' ======================= the screen ==================================

' Where each field lives: label row, then the value.  Kept in one place
' so the layout is readable rather than scattered through the loop.
Sub layout
  drawstr(8, 4, "PC3 QNH STATION", 3, GREY, BLACK)
  fillrect(0, 32, SW, 2, GREY)

  drawstr(8, 96, "DATE", 1, GREY, BLACK)
  drawstr(8, 140, "TEMPERATURE", 1, GREY, BLACK)
  drawstr(8, 184, "STATION PRESSURE", 1, GREY, BLACK)
  drawstr(8, 228, "HEIGHT ASL", 1, GREY, BLACK)
  ' The unit lives on the label, not on the value: at 24 pixels a
  ' character, "1013.2 hPa" is ten of them and the panel is 240 wide.
  drawstr(8, 272, "QNH  hPa", 1, GREY, BLACK)
End Sub

' Draw a field only if it changed, and pad to the width it had before
' so a number that got shorter does not leave its old tail behind.
Sub show(n As Integer, s$, f As Integer, col As Integer)
  Local Integer y(8), i
  Local t$

  y(0) = 44 : y(1) = 110 : y(2) = 154
  y(3) = 198 : y(4) = 242 : y(5) = 286

  t$ = s$
  If Len(t$) < Len(last$(n)) Then
    For i = Len(t$) + 1 To Len(last$(n))
      t$ = t$ + " "
    Next i
  EndIf
  If t$ = last$(n) Then Exit Sub
  drawstr(8, y(n), t$, f, col, BLACK)
  last$(n) = s$
End Sub

' ======================= the potentiometer ===========================

Sub setup_pot
  ' AIN gives volts, filtered as MMBasic filters it: ten readings,
  ' sorted, the top two and bottom two thrown away and the remaining
  ' six averaged, scaled 3.3 V over 4095.  On the RP2350B channel n is
  ' GP40+n, so this is channel 1.
  SetPin POTPIN, AIN
  Print "pot on GP"; Str$(POTPIN); " reads "; Str$(Pin(POTPIN), 0, 3); " V"
End Sub

' ======================= the BMP180 ==================================
'
' The sensor's arithmetic is Bosch's, out of the datasheet, and this is
' MMBasic's BMP180 program unchanged apart from the bus: the part is on
' the I/O header rather than the QWIIC socket, so SETPIN names the pins
' and I2C2 opens the second controller.

Sub setup_sensor
  SetPin SDAPIN, SCLPIN, I2C2
  I2C2 Open 400, 1000

  OSS = 1
  OSSdata(0) = &H34 : OSSdata(1) = &H74
  OSSdata(2) = &HB4 : OSSdata(3) = &HF4
  OSSscale(0) = 1 : OSSscale(1) = 2
  OSSscale(2) = 4 : OSSscale(3) = 8

  I2C2 Write BMPADDR, 1, 1, &HAA          ' read the calibration block
  I2C2 Read BMPADDR, 0, 22, i2cin$
  ac1 = Str2bin(int16,  Mid$(i2cin$, 1, 2), big)
  ac2 = Str2bin(int16,  Mid$(i2cin$, 3, 2), big)
  ac3 = Str2bin(int16,  Mid$(i2cin$, 5, 2), big)
  ac4 = Str2bin(uint16, Mid$(i2cin$, 7, 2), big)
  ac5 = Str2bin(uint16, Mid$(i2cin$, 9, 2), big)
  ac6 = Str2bin(uint16, Mid$(i2cin$, 11, 2), big)
  b1  = Str2bin(int16,  Mid$(i2cin$, 13, 2), big)
  b2  = Str2bin(int16,  Mid$(i2cin$, 15, 2), big)
  mb  = Str2bin(int16,  Mid$(i2cin$, 17, 2), big)
  mc  = Str2bin(int16,  Mid$(i2cin$, 19, 2), big)
  md  = Str2bin(int16,  Right$(i2cin$, 2), big)
  Print "BMP180 calibration read, ac1="; Str$(ac1); " md="; Str$(md)
End Sub

Sub read_bmp180
  I2C2 Write BMPADDR, 0, 2, &HF4, &H2E    ' start a temperature conversion
  Pause 7
  I2C2 Write BMPADDR, 1, 1, &HF6
  I2C2 Read BMPADDR, 0, 2, i2cin$
  UT = Str2bin(uint16, i2cin$, big)

  I2C2 Write BMPADDR, 0, 2, &HF4, OSSdata(OSS)
  Pause (OSS + 1) * 7
  I2C2 Write BMPADDR, 1, 1, &HF6
  I2C2 Read BMPADDR, 0, 3, i2cin$
  UP = Str2bin(uint32, Chr$(0) + i2cin$, big)
  UP = UP >> (8 - OSS)                    ' the unused bits of the xlsb

  calc_temp()
  calc_pressure()
End Sub

Sub calc_temp
  x1 = (UT - ac6) * ac5 \ powerof2(15)
  x2 = mc * powerof2(11) / (x1 + md)      ' a floating divide, as Bosch has it
  b5 = x1 + x2
  temperature = (b5 + 8) \ powerof2(4)
End Sub

Sub calc_pressure
  b6 = b5 - 4000
  x1 = (b2 * (b6 * b6 / powerof2(12))) \ powerof2(11)
  x2 = ac2 * b6 \ powerof2(11)
  x3 = x1 + x2
  b3 = (((ac1 * 4 + x3) * OSSscale(OSS)) + 2) \ 4
  x1 = ac3 * b6 \ powerof2(13)
  x2 = (b1 * (b6 * b6 / powerof2(12))) \ powerof2(16)
  x3 = ((x1 + x2) + 2) \ 4
  b4 = ac4 * (Abs(x3 + 32768)) \ powerof2(15)
  b7 = Abs(UP - b3) * (50000 \ OSSscale(OSS))
  pressure = (b7 * 2) \ b4
  x1 = (pressure \ powerof2(8)) * (pressure \ powerof2(8))
  x1 = (x1 * 3038) \ powerof2(16)
  x2 = (-7357 * pressure) \ powerof2(16)
  pressure = pressure + (x1 + x2 + 3791) \ powerof2(4)
End Sub

Function powerof2(i As Integer) As Integer
  powerof2 = (1 << i)
End Function

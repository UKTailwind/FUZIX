' flashpix - pseudo flash slots and the buffer BLITs, on hardware.
' Builds a slot image file from BASIC (PicoMite packing: low nibble is
' the left pixel), loads it, blits it, and verifies by PIXEL readback
' against the same pattern painted by hand.  Zeros are a pass.
MODE 2
CLS RGB(0,0,0)

' the slot file: 6x3, pixel (x,y) = (x+y+1) AND 15, 8-byte header
OPEN "fimg.bin" FOR OUTPUT AS #1
PRINT #1, CHR$(6); CHR$(0); CHR$(0); CHR$(0);
PRINT #1, CHR$(3); CHR$(0); CHR$(0); CHR$(0);
FOR y% = 0 TO 2
  FOR x% = 0 TO 5 STEP 2
    b% = ((x%+y%+1) AND 15) + (((x%+1+y%+1) AND 15) * 16)
    PRINT #1, CHR$(b%);
  NEXT x%
NEXT y%
CLOSE #1

FLASH ERASE 1
FLASH DISK LOAD 1, "fimg.bin", O
BLIT FLASH 1, N, 0, 0, 40, 40, 6, 3

' the same pattern painted by hand is the reference
FOR y% = 0 TO 2
  FOR x% = 0 TO 5
    PIXEL 60+x%, 40+y%, MAP((x%+y%+1) AND 15)
  NEXT x%
NEXT y%
f% = 0
FOR y% = 0 TO 2
  FOR x% = 0 TO 5
    IF PIXEL(40+x%,40+y%) <> PIXEL(60+x%,40+y%) THEN f% = f% + 1
  NEXT x%
NEXT y%
r1% = f%

' the buffer form: N->F, then F back onto N somewhere else
FRAMEBUFFER CREATE
BLIT FRAMEBUFFER N, F, 60, 40, 10, 10, 6, 3
BLIT FRAMEBUFFER F, N, 10, 10, 100, 40, 6, 3
f% = 0
FOR y% = 0 TO 2
  FOR x% = 0 TO 5
    IF PIXEL(100+x%,40+y%) <> PIXEL(60+x%,40+y%) THEN f% = f% + 1
  NEXT x%
NEXT y%
r2% = f%
FRAMEBUFFER CLOSE F
KILL "fimg.bin"

MODE 1
PRINT "flash blit : "; r1%
PRINT "fb blit    : "; r2%
PRINT "flashpix done"

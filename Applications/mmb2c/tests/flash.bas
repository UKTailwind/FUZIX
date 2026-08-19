' Pseudo flash slots under the gates: allocation, the guards, and the
' new BLIT forms' argument surface.  Pixels are the harness's and the
' board's business.
MODE 2

' the address allocates the slot lazily and is stable
a% = MM.INFO(FLASH ADDRESS 1)
b% = MM.INFO(FLASH ADDRESS 1)
IF a% <> 0 AND a% = b% THEN PRINT "addr ok"

' a file to load
OPEN "slot.bin" FOR OUTPUT AS #1
PRINT #1, "hello";
CLOSE #1

' an erased slot loads without O; a programmed one is the error
FLASH DISK LOAD 1, "slot.bin"
ON ERROR SKIP 1
FLASH DISK LOAD 1, "slot.bin"
PRINT "1: " MM.ERRMSG$

' with O it goes; after ERASE it loads clean again
FLASH DISK LOAD 1, "slot.bin", O
FLASH ERASE 1
FLASH DISK LOAD 1, "slot.bin"

' slot 4 is out of range
ON ERROR SKIP 1
FLASH ERASE 4
PRINT "2: " MM.ERRMSG$

' an erased slot is not an image (slot 1 again: the host VM is 128K
' all told, so the gates keep to one live slot)
FLASH ERASE 1
ON ERROR SKIP 1
BLIT FLASH 1, N, 0, 0, 10, 10, 4, 4
PRINT "3: " MM.ERRMSG$

' the FRAMEBUFFER form's surface (headless: validated, then silent)
FRAMEBUFFER CREATE
BLIT FRAMEBUFFER N, F, 0, 0, 5, 5, 8, 8
BLIT FRAMEBUFFER F, N, 0, 0, 5, 5, 8, 8, 3
FRAMEBUFFER CLOSE F

' a source rectangle outside the screen is a hard error
ON ERROR SKIP 1
BLIT FRAMEBUFFER N, F, 500, 0, 0, 0, 8, 8
PRINT "4: " MM.ERRMSG$

PRINT "flash surface ok"

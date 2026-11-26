' BLIT FRAMEBUFFER and BLIT FLASH with a width or height below 1 draw
' nothing, as on the firmware (PicoMite 37e7af5, 10-21 #25): a width
' worked out as 0 stopped the program with "Invalid coordinates".  A
' source off the buffer or the image is still refused.
MODE 2
FRAMEBUFFER CREATE
For w = 1 To -2 Step -1
  BLIT FRAMEBUFFER N, F, 0, 0, 5, 5, w, 8
  BLIT FRAMEBUFFER F, N, 0, 0, 5, 5, 8, w
Next
Print "framebuffer"
On Error Skip 1
BLIT FRAMEBUFFER N, F, -1, 0, 0, 0, 0, 8
Print MM.ErrMsg$
FRAMEBUFFER CLOSE F

' a 4 by 4 image: its width and height, then 2 pixels a byte
Open "img.bin" For Output As #1
Print #1, Chr$(4); String$(3, 0); Chr$(4); String$(3, 0); String$(8, &H12);
Close #1
FLASH DISK LOAD 1, "img.bin", O
For w = 1 To -2 Step -1
  BLIT FLASH 1, N, 0, 0, 10, 10, w, 4
  BLIT FLASH 1, N, 0, 0, 10, 10, 4, w
Next
Print "flash"
On Error Skip 1
BLIT FLASH 1, N, 1, 0, 10, 10, 4, 0
Print MM.ErrMsg$
FLASH ERASE 1

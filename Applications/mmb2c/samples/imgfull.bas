' Full-screen save/restore, five rounds - the heavy version of imgloop.
'
' 640x480 as a 24-bit BMP is 921,654 bytes = 1800 blocks, so each file
' needs all 18 direct blocks, all 256 single indirect, and about 1526
' double indirect entries over six level-2 blocks.  imgloop only ever
' touched the FIRST double indirect entry; this walks the whole
' structure, and allocates and truncates it ten times.  That is what
' f_trunc used to corrupt - it freed the double indirect root and left
' i_addr[19] pointing at it, so the rewrite reused a freed block as the
' root and read picture data as block pointers.
'
' NOTHING IS PRINTED INSIDE THE LOOP, deliberately.  In MODE 1 the
' console and the graphics share one framebuffer, so a SYSTEM "sum"
' between rounds writes text onto the very screen the next SAVE IMAGE
' photographs.  The first version did that and its checksums differed
' from byte 714294 on - row 372 of a bottom-up BMP, i.e. screen row 107
' upwards, the console band.  imgloop never noticed because its window
' was rows 120-359 and missed it.
'
' Kept as a LOOP rather than unrolled: unrolling took the bytecode from
' 11,567 to 23,931 bytes, and bcrun then could not fork - the child has
' to be allocated while the parent is still resident, and two copies of
' a bcrun that big will not fit in 316K.
'
' Afterwards, from the shell:  cmp a.bmp b.bmp   (and fsck on reboot)
MODE 1
For n = 1 To 5
  For y = 0 To 479
    Line 0, y, 639, y, , 0
  Next y
  Circle 320, 240, 200, , , RGB(WHITE)
  Circle 320, 240, 120, , , RGB(WHITE), RGB(WHITE)
  Circle 160, 120, 60, 5, , RGB(WHITE)
  Circle 480, 360, 60, 5, , RGB(WHITE)
  For x = 0 To 639 Step 40
    Line x, 0, x, 479, , RGB(WHITE)
  Next x
  SAVE IMAGE "a.bmp"
  For y = 0 To 479
    Line 0, y, 639, y, , 0
  Next y
  LOAD IMAGE "a.bmp", 0, 0
  SAVE IMAGE "b.bmp"
Next n
Print "five full-screen rounds done - now cmp a.bmp b.bmp"

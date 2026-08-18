' blitbench - the 4bpp pixel engine, path by path.
'
' The A/B instrument for moving mmb_blit.h's row workhorses into the
' kernel.  Every case below runs through mmb_blit.h, and between them
' they reach each of its distinct inner loops, because the change is
' expected to help them by very different amounts:
'
'   aligned opaque   the nibble-swap memcpy path - already the fastest,
'                    so the least to gain and the best regression check
'   odd x            the staged unaligned merge, a shift per byte
'   keyed            the transparent test, per byte
'   RLE              mmb_pack_row_rle, a run at a time
'   READ / WRITE     mmb_row_get / mmb_row_put, one byte per pixel
'   COPY             read and write of the same rectangle
'   SPRITE           the sprite path, which uses the same row calls
'
' Sizes and repeat counts are chosen so each case is a few hundred ms
' at v0.16 speed: long enough that the 1ms Timer is noise, short enough
' that the whole run is well under a minute.  Pixel counts are exact,
' so ns/px is comparable ACROSS cases as well as between builds.
'
' Read pc3-benchmark-method before quoting a number from this: fresh
' boot, output to the screen, and the two builds interleaved in ONE
' session.  A long session drifts by ~11% all on its own.

Mode 2
Cls Rgb(0,0,0)

Open "/dev/tty" For output As #2

Dim integer tile(40)      ' 24x24 uncompressed, 4bpp  (4 + 288 bytes)
Dim integer rlet(20)      ' 24x24 RLE                 (4 + 72 bytes)
Dim integer i, x, y, n, t0, t1, a, r
Dim float total

' ---- the sources ----------------------------------------------------
' Header is two 16-bit words, w then h; bit 15 of either says RLE.
' Uncompressed data is 4bpp, two pixels per byte, LOW nibble the LEFT
' pixel - the mirror of the framebuffer's packing, which is why the
' aligned fast path is a nibble swap and not a memcpy.

a = Peek(varaddr tile())
Poke short a, 24
Poke short a + 2, 24
For i = 0 To 287
  Poke byte a + 4 + i, ((i Mod 15) + 1) * 17    ' both nibbles the same
Next i

' RLE: one byte per run, colour in the high nibble, count in the low.
' 24x24 = 576 pixels as 72 runs of 8.
r = Peek(varaddr rlet())
Poke short r, 24 Or &h8000
Poke short r + 2, 24
For i = 0 To 71
  Poke byte r + 4 + i, ((i Mod 15) + 1) * 16 + 8
Next i

Print #2, "case                     reps    pixels     ms    ns/px"
Print #2, "------------------------------------------------------"
total = 0

' ---- 1. BLIT MEMORY, even x, opaque: the nibble-swap path -----------
n = 1500
t0 = Timer
For i = 1 To n
  Blit memory a, 24 * (i Mod 12), 24 * (i Mod 9)
Next i
t1 = Timer
report "blit memory aligned", n, n * 576, t1 - t0

' ---- 2. BLIT MEMORY at an ODD x: the staged unaligned merge ---------
n = 1500
t0 = Timer
For i = 1 To n
  Blit memory a, 24 * (i Mod 12) + 1, 24 * (i Mod 9)
Next i
t1 = Timer
report "blit memory odd x", n, n * 576, t1 - t0

' ---- 3. BLIT MEMORY keyed: the per-byte transparency test -----------
n = 1500
t0 = Timer
For i = 1 To n
  Blit memory a, 24 * (i Mod 12), 24 * (i Mod 9), 7
Next i
t1 = Timer
report "blit memory keyed", n, n * 576, t1 - t0

' ---- 4. BLIT COMPRESSED: a run at a time ----------------------------
n = 1000
t0 = Timer
For i = 1 To n
  Blit compressed r, 24 * (i Mod 12), 24 * (i Mod 9)
Next i
t1 = Timer
report "blit compressed", n, n * 576, t1 - t0

' ---- 5. BLIT READ: mmb_row_get, framebuffer to buffer ---------------
n = 600
t0 = Timer
For i = 1 To n
  Blit read #1, 32 * (i Mod 8), 32 * (i Mod 6), 32, 32
  Blit close #1
Next i
t1 = Timer
report "blit read 32x32", n, n * 1024, t1 - t0

' ---- 6. BLIT WRITE: mmb_row_put -------------------------------------
Blit read #1, 0, 0, 32, 32
n = 600
t0 = Timer
For i = 1 To n
  Blit write #1, 32 * (i Mod 8), 32 * (i Mod 6)
Next i
t1 = Timer
report "blit write 32x32", n, n * 1024, t1 - t0

' ---- 7. BLIT WRITE mirrored: the same, walked backwards -------------
n = 600
t0 = Timer
For i = 1 To n
  Blit write #1, 32 * (i Mod 8), 32 * (i Mod 6), 3
Next i
t1 = Timer
report "blit write mirror", n, n * 1024, t1 - t0
Blit close #1

' ---- 8. BLIT rectangle: screen to screen ----------------------------
n = 400
t0 = Timer
For i = 1 To n
  Blit 0, 0, 32 * (i Mod 8), 32 * (i Mod 6), 32, 32
Next i
t1 = Timer
report "blit copy 32x32", n, n * 1024, t1 - t0

' ---- 9. SPRITE: the same row calls, from the sprite side ------------
n = 500
t0 = Timer
For i = 1 To n
  Sprite memory a, 24 * (i Mod 12), 24 * (i Mod 9), 1
Next i
t1 = Timer
report "sprite memory 24x24", n, n * 576, t1 - t0

Print #2, "------------------------------------------------------"
Print #2, "TOTAL " total " ms"

Close #2
End

' ---------------------------------------------------------------------
Sub report(what$, reps As integer, px As integer, ms As integer)
  Local float nsp

  If px > 0 And ms > 0 Then
    nsp = ms * 1000000.0 / px
  Else
    nsp = 0
  End If
  Print #2, Left$(what$ + Space$(22), 22);
  Print #2, Str$(reps, 6); Str$(px, 10); Str$(ms, 7); Str$(nsp, 9, 1)
  total = total + ms
End Sub

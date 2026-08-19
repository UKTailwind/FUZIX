' Blit every tile in PETSCII Robots' own sprite library, one at a time,
' saying which one it is BEFORE it draws it.
'
' The game dies silently on the way out of the splash screen, and that
' is exactly where it first blits real sprite data.  The blitter was
' rewritten to work nibble-to-nibble, and its harness encodes its own
' RLE with runs capped under 15 - so a stored count of 0, which means a
' run of 256, has never once been through the new code.  This is the
' real data, so whatever the library actually contains gets tested.
'
' If it stops, the last number printed is the tile that did it.

Mode 2
FrameBuffer Create
FrameBuffer Layer 9
Open "/dev/tty" For output As #2

Dim integer fl_adr, i, w, h
Dim a$

Flash disk load 3, "lib/pet_lib23.bin", o
fl_adr = Mm.Info(flash address 3)
Print #2, "slot at "; fl_adr

Dim tile_index(&hff)
Open "lib/flash_index.txt" For input As #1
For i = 0 To &hFF
  Input #1, a$
  tile_index(i) = Val(a$) + fl_adr
Next
Close #1
Print #2, "index loaded"

' the headers first, without drawing: width, height and the RLE bit
For i = 0 To &hFF
  w = Peek(short tile_index(i))
  h = Peek(short tile_index(i) + 2)
  If (w And &h8000) Or (h And &h8000) Then
    Print #2, "tile "; i; " "; (w And &h7fff); "x"; (h And &h7fff); " RLE"
  Else
    Print #2, "tile "; i; " "; w; "x"; h; " raw"
  EndIf
Next
Print #2, "headers ok"

' now draw them
FrameBuffer Write f
For i = 0 To &hFF
  Print #2, "draw "; i
  Blit memory tile_index(i), (i Mod 12) * 24, ((i \ 12) Mod 9) * 24
Next
Print #2, "ALL TILES DREW OK"

' and again with a transparent colour, which takes the other path
For i = 0 To &hFF
  Print #2, "keyed "; i
  Blit memory tile_index(i), (i Mod 12) * 24, ((i \ 12) Mod 9) * 24, 9
Next
Print #2, "ALL KEYED OK"

Close #2

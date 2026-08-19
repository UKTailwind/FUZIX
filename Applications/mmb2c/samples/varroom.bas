' How much variable space does a translated BASIC program have?
'
' A framebuffer-sized array: 640x480 at 1bpp and 320x240 at 4bpp are
' both 38,400 bytes. MMINTEGER is 8 bytes, so 4800 elements is 38,408
' with OPTION BASE 0. Every element is touched so the space is really
' claimed rather than merely declared.
'
' free/ps are run through SYSTEM so they report while this program is
' resident - the parent in the ps listing is bcrun with the array in
' it, which is the number that matters.
Dim Integer fb(4800)
For i = 0 To 4800
  fb(i) = i
Next i
Print "claimed"; (4801 * 8); " bytes of array"
Print "checksum"; fb(0) + fb(2400) + fb(4800)
SYSTEM "free"
SYSTEM "ps"
Print "varroom done"

' The other refusal: drawing into a buffer that was never created.
'
' Its own program because mm_error exits - see framebuf.bas, which
' covers the rest of the rules.

Print "-- write N without a buffer is fine --"
FrameBuffer Write N
Print "  ok"

Print "-- write F without one is not --"
FrameBuffer Write F
Print "  NOT REACHED"

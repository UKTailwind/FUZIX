' FRAMEBUFFER: the rules, checked where there is no screen.
'
' The buffer itself needs hardware, but WHEN a program is allowed to
' create, write to, copy and close one does not - those rules are in
' the runtime and are the same on the host and on the board.  So this
' is the half that can be gated: the sequence a real program follows,
' and the two things that must be refused.
'
' It ends on a deliberate error, which is the only way to check one:
' mm_error prints and exits, so a program gets a single go at it.

Print "-- create --"
FrameBuffer Create
Print "  created"

Print "-- write --"
FrameBuffer Write F
Print "  drawing into F"
FrameBuffer Write N
Print "  drawing into N"

Print "-- copy both ways --"
FrameBuffer Write F
FrameBuffer Copy F, N
Print "  F to N"
FrameBuffer Copy N, F
Print "  N to F"
FrameBuffer Copy F, N, B
Print "  F to N at the top of the frame"

Print "-- close --"
FrameBuffer Close F
Print "  closed"

' Closing one that is not there is deliberately quiet: a program that
' tidies up unconditionally at the end is the normal shape, and
' MMBasic's closeframebuffer simply finds nothing to free.
FrameBuffer Close
Print "  closing again is quiet"

Print "-- create after close --"
FrameBuffer Create
Print "  created"

' A mode change throws the buffer away - its contents are in the
' geometry of the mode being left and nothing converts them.  So this
' Create must succeed; if the mode change had NOT discarded it, it
' would be the error at the bottom of this file instead.
Print "-- mode change discards it --"
Mode 2
FrameBuffer Create
Print "  created again after MODE"

Print "-- creating twice is an error --"
FrameBuffer Create
Print "  NOT REACHED"

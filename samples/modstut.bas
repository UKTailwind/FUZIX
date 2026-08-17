' Does a MOD playing in the background cost the PROGRAM time?
'
' playmod keeps ~93ms of audio queued (TARGET_BYTES) and tops it up
' every 20ms.  If the music chops, playmod is not being scheduled - and
' the question is whether that is the daemon competing for the CPU or
' the machine SWAPPING one of them to the SD card to make room.
'
' A swap shows up here: the same loop, timed with the music off and
' then on.  Costing nothing means the daemon is cheap and the problem
' is elsewhere (size).  Costing a lot in a program THIS SMALL means it
' is not about size at all.

Dim integer i, x, t0, t1, quiet, music

t0 = Timer
For i = 1 To 400000 : x = x + i : Next
t1 = Timer
quiet = t1 - t0
Print "quiet loop "; quiet; " ms"

Play modfile "/root/robots/music/metal_heads-sfx.mod"
Pause 500

t0 = Timer
For i = 1 To 400000 : x = x + i : Next
t1 = Timer
music = t1 - t0
Print "music loop "; music; " ms"

Play stop
Print "overhead "; music - quiet; " ms ("; (100 * music) / quiet; "% of quiet)"

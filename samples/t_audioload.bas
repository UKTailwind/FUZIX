' What starves the MOD player: CPU, or graphics?
'
' playmod alone is clean.  playmod under PETSCII Robots pulses.  The
' vsync spin is now yielded and it made no difference, so the cause is
' something else - and the two candidates are simply "another process
' wants the CPU" and "something about the graphics path in particular".
'
' Three phases, each about fifteen seconds, announced on /dev/tty so
' you know which one you are listening to:
'
'   1  pure computation, no graphics at all
'   2  graphics with no computation - merge after merge
'   3  quiet: the music with nothing else running
'
' Listen to each.  Whichever phases pulse is the answer.

Mode 2
FrameBuffer Create
FrameBuffer Layer 9
Open "/dev/tty" For output As #2

Dim integer i, j, t0
Dim x

Play modfile "/root/robots/music/metal_heads-sfx.mod"
Pause 2000

Print #2, "PHASE 1: pure compute, no graphics - 15s"
t0 = Timer
Do
  For i = 1 To 20000
    x = x + i * 1.5
  Next
Loop Until Timer - t0 > 15000
Print #2, "  phase 1 done"

Print #2, "PHASE 2: merges only, no compute - 15s"
t0 = Timer
Do
  FrameBuffer Merge 9, b
Loop Until Timer - t0 > 15000
Print #2, "  phase 2 done"

Print #2, "PHASE 3: quiet, music alone - 15s"
t0 = Timer
Do
  Pause 1000
Loop Until Timer - t0 > 15000
Print #2, "  phase 3 done"

Play stop
Print #2, "done"
Close #2
